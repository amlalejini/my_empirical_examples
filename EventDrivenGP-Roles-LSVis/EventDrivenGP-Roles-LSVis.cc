
#include <iostream>
#include <sstream>
#include <map>
#include <unordered_set>
#include <set>
#include <string>
#include <utility>
#include "base/Ptr.h"
#include "hardware/EventDrivenGP.h"
#include "hardware/InstLib.h"
#include "hardware/EventLib.h"
#include "tools/map_utils.h"

#include "web/init.h"
#include "web/JSWrap.h"
#include "web/Document.h"
#include "web/Animate.h"
#include "web/web.h"
#include "web/d3/visualizations.h"
#include "web/d3/dataset.h"

// @amlalejini - TODO:
// [ ] Switch to using linear scales
// [ ] Make sizing of everything dynamic
// [ ] Wrap x/y/text things in named functions
// [ ] Move all styling to SCSS
// [ ] Beautify (with SASS)
// [ ] Put in place safety features
//    [ ] What if we've knocked out the entire program?
//    [ ] Protect vis functions from getting called in unexpected order.
// [ ] Parameterize everything.


using event_lib_t = typename emp::EventDrivenGP::event_lib_t;
using event_t = typename emp::EventDrivenGP::event_t;
using inst_lib_t = typename::emp::EventDrivenGP::inst_lib_t;
using inst_t = typename::emp::EventDrivenGP::inst_t;
using affinity_t = typename::emp::EventDrivenGP::affinity_t;
using program_t = emp::EventDrivenGP::Program;
using fun_t = emp::EventDrivenGP::Function;
using state_t = emp::EventDrivenGP::State;

constexpr size_t EVAL_TIME = 200;
constexpr size_t DIST_SYS_WIDTH = 5;
constexpr size_t DIST_SYS_HEIGHT = 5;
constexpr size_t DIST_SYS_SIZE = DIST_SYS_WIDTH * DIST_SYS_HEIGHT;

constexpr size_t TRAIT_ID__ROLE_ID = 0;
constexpr size_t TRAIT_ID__X_LOC = 1;
constexpr size_t TRAIT_ID__Y_LOC = 2;

constexpr size_t CPU_SIZE = emp::EventDrivenGP::CPU_SIZE;
constexpr size_t MAX_INST_ARGS = emp::EventDrivenGP::MAX_INST_ARGS;

constexpr int DEFAULT_RANDOM_SEED = -1;


// This will be the target of evolution (what the world manages/etc.)
struct Agent {
  size_t valid_uid_cnt;
  size_t valid_id_cnt;
  program_t program;

  Agent(emp::Ptr<inst_lib_t> _ilib)
  : valid_uid_cnt(0), valid_id_cnt(0), program(_ilib) { ; }

  Agent(const program_t & _program)
  : valid_uid_cnt(0), valid_id_cnt(0), program(_program) { ; }

};

// Deme structure for holding distributed system.
struct Deme {
  using hardware_t = emp::EventDrivenGP;
  using memory_t = typename emp::EventDrivenGP::memory_t;
  using grid_t = emp::vector<emp::Ptr<hardware_t>>;
  using pos_t = std::pair<size_t, size_t>;

  grid_t grid;
  size_t width;
  size_t height;
  emp::Ptr<emp::Random> rnd;
  emp::Ptr<event_lib_t> event_lib;
  emp::Ptr<inst_lib_t> inst_lib;

  emp::Ptr<Agent> agent_ptr;
  bool agent_loaded;

  Deme(emp::Ptr<emp::Random> _rnd, size_t _w, size_t _h, emp::Ptr<event_lib_t> _elib, emp::Ptr<inst_lib_t> _ilib)
    : grid(_w * _h), width(_w), height(_h), rnd(_rnd), event_lib(_elib), inst_lib(_ilib), agent_ptr(nullptr), agent_loaded(false) {
    // Register dispatch function.
    event_lib->RegisterDispatchFun("Message", [this](hardware_t & hw_src, const event_t & event){ this->DispatchMessage(hw_src, event); });
    // Fill out the grid with hardware.
    for (size_t i = 0; i < width * height; ++i) {
      grid[i].New(inst_lib, event_lib, rnd);
      pos_t pos = GetPos(i);
      grid[i]->SetTrait(TRAIT_ID__ROLE_ID, 0);
      grid[i]->SetTrait(TRAIT_ID__X_LOC, pos.first);
      grid[i]->SetTrait(TRAIT_ID__Y_LOC, pos.second);
    }
  }

  ~Deme() {
    Reset();
    for (size_t i = 0; i < grid.size(); ++i) {
      grid[i].Delete();
    }
    grid.resize(0);
  }

  void Reset() {
    agent_ptr = nullptr;
    agent_loaded = false;
    for (size_t i = 0; i < grid.size(); ++i) {
      grid[i]->ResetHardware();
      grid[i]->SetTrait(TRAIT_ID__ROLE_ID, 0);
    }
  }

  void LoadAgent(emp::Ptr<Agent> _agent_ptr) {
    Reset();
    agent_ptr = _agent_ptr;
    for (size_t i = 0; i < grid.size(); ++i) {
      grid[i]->SetProgram(agent_ptr->program);
      grid[i]->SpawnCore(0, memory_t(), true);
    }
    agent_loaded = true;
  }

  size_t GetWidth() const { return width; }
  size_t GetHeight() const { return height; }

  pos_t GetPos(size_t id) { return pos_t(id % width, id / width); }
  size_t GetID(size_t x, size_t y) { return (y * width) + x; }

  void Print(std::ostream & os=std::cout) {
    os << "=============DEME=============\n";
    for (size_t i = 0; i < grid.size(); ++i) {
      os << "--- Agent @ (" << GetPos(i).first << ", " << GetPos(i).second << ") ---\n";
      grid[i]->PrintState(os); os << "\n";
    }
  }

  void DispatchMessage(hardware_t & hw_src, const event_t & event) {
    const size_t x = (size_t)hw_src.GetTrait(TRAIT_ID__X_LOC);
    const size_t y = (size_t)hw_src.GetTrait(TRAIT_ID__Y_LOC);
    emp::vector<size_t> recipients;
    if (event.HasProperty("send")) {
      // Send to random neighbor.
      recipients.push_back(GetRandomNeighbor(GetID(x, y)));
    } else {
      // Treat as broadcast, send to all neighbors.
      recipients.push_back(GetID((size_t)emp::Mod((int)x - 1, (int)width), (size_t)emp::Mod((int)y, (int)height)));
      recipients.push_back(GetID((size_t)emp::Mod((int)x + 1, (int)width), (size_t)emp::Mod((int)y, (int)height)));
      recipients.push_back(GetID((size_t)emp::Mod((int)x, (int)width), (size_t)emp::Mod((int)y - 1, (int)height)));
      recipients.push_back(GetID((size_t)emp::Mod((int)x, (int)width), (size_t)emp::Mod((int)y + 1, (int)height)));
    }
    // Dispatch event to recipients.
    for (size_t i = 0; i < recipients.size(); ++i)
      grid[recipients[i]]->QueueEvent(event);
  }

  // Pulled this function from PopMng_Grid.
  size_t GetRandomNeighbor(size_t id) {
    const int offset = rnd->GetInt(9);
    const int rand_x = (int)(id%width) + offset%3-1;
    const int rand_y = (int)(id/width) + offset/3-1;
    return ((size_t)emp::Mod(rand_x, (int)width) + (size_t)emp::Mod(rand_y, (int)height)*width);
  }


  void Advance(size_t t=1) { for (size_t i = 0; i < t; ++i) SingleAdvance(); }

  void SingleAdvance() {
    emp_assert(agent_loaded);
    for (size_t i = 0; i < grid.size(); ++i) {
      grid[i]->SingleProcess();
    }
  }

};

// Some extra instructions for this experiment.
void Inst_GetRoleID(emp::EventDrivenGP & hw, const inst_t & inst) {
  state_t & state = *hw.GetCurState();
  state.SetLocal(inst.args[0], hw.GetTrait(TRAIT_ID__ROLE_ID));
}

void Inst_SetRoleID(emp::EventDrivenGP & hw, const inst_t & inst) {
  state_t & state = *hw.GetCurState();
  hw.SetTrait(TRAIT_ID__ROLE_ID, (int)state.AccessLocal(inst.args[0]));
}

void Inst_GetXLoc(emp::EventDrivenGP & hw, const inst_t & inst) {
  state_t & state = *hw.GetCurState();
  state.SetLocal(inst.args[0], hw.GetTrait(TRAIT_ID__X_LOC));
}

void Inst_GetYLoc(emp::EventDrivenGP & hw, const inst_t & inst) {
  state_t & state = *hw.GetCurState();
  state.SetLocal(inst.args[0], hw.GetTrait(TRAIT_ID__Y_LOC));
}


namespace emp {
namespace web {

// @amlalejini - NOTE: This is inspired by/mirrors Emily's D3Visualizations from web/d3/visualizations.h
class EventDrivenGP_ProgramVis : public D3Visualization {

protected:

  std::map<std::string, program_t> program_map;     // Map of available programs.
  Ptr<program_t> cur_program;                       // Program built from program data.
  std::string display_program;

  double y_margin;
  double x_margin;

  double inst_blk_height = 50;
  double inst_blk_width = 150;
  double func_blk_width = 200;

  std::set<std::pair<int, int>> inst_knockouts;
  std::set<int> func_knockouts;


  std::function<void(int, int)> knockout_inst = [this](int fp, int ip) {
    std::pair<int, int> inst_loc(fp, ip);
    if (inst_knockouts.count(inst_loc))
      inst_knockouts.erase(inst_loc);
    else
      inst_knockouts.insert(inst_loc);
  };

  std::function<void(int)> knockout_func = [this](int fp) {
    if (func_knockouts.count(fp))
      func_knockouts.erase(fp);
    else
      func_knockouts.insert(fp);
  };

  void InitializeVariables() {
    std::cout << "Init vars" << std::endl;
    JSWrap(knockout_func, "knockout_func");
    JSWrap(knockout_inst, "knockout_inst");
    JSWrap([this](){ return this->program_data->GetID(); }, "get_prog_data_obj_id");
    JSWrap([this](){ return this->GetSVG()->GetID(); }, "get_prog_vis_svg_obj_id");
    EM_ASM({ window.addEventListener("resize", resizeProgVis); });
    GetSVG()->Move(0, 0);
  }

  void SetProgramData(const std::string & name) {
    emp_assert(Has(program_map, name));
    std::cout << "Set program data to: " << name << std::endl;
    if (program_data) program_data.Delete();
    const program_t & program = program_map.at(name);
    size_t cum_inst_cnt = 0;
    std::stringstream p_data;
    p_data << "{\"name\":\"" << name << "\",\"functions\":[";
    // For each function, append data.
    for (size_t fID = 0; fID < program.GetSize(); ++fID) {
      if (fID != 0) p_data << ",";
      p_data << "{\"affinity\":\""; program[fID].affinity.Print(p_data); p_data << "\",";
      p_data << "\"sequence_len\":" << program[fID].GetSize() << ",";
      p_data << "\"cumulative_seq_len\":" << cum_inst_cnt << ",";
      p_data << "\"sequence\":[";
      for (size_t iID = 0; iID < program[fID].GetSize(); ++iID) {
        if (iID != 0) p_data << ",";
        const inst_t & inst = program[fID].inst_seq[iID];
        p_data << "{\"name\":\"" << program.inst_lib->GetName(inst.id) << "\",";
        p_data << "\"position\":" << iID << ",";
        p_data << "\"function\":" << fID << ",";
        p_data << "\"has_affinity\":" << program.inst_lib->HasProperty(inst.id, "affinity") << ",";
        p_data << "\"num_args\":" << program.inst_lib->GetNumArgs(inst.id) << ",";
        p_data << "\"affinity\":\""; inst.affinity.Print(p_data); p_data << "\",";
        p_data << "\"args\":[" << inst.args[0] << "," << inst.args[1] << "," << inst.args[2] << "]";
        p_data << "}";
      }
      p_data << "]";
      p_data << "}";
      cum_inst_cnt += program[fID].GetSize();
    }
    p_data << "]}";
    program_data = NewPtr<D3::JSONDataset>();
    program_data->Append(p_data.str());
  }

  void DisplayProgram(const std::string & name) {
    emp_assert(Has(program_map, name));
    std::cout << "Display program: " << name << std::endl;
    display_program = name;
    // Reset knockouts
    ResetKnockouts();
    // Set program data to correct program
    SetProgramData(name);
    // Draw program from program data.
    DrawProgram();
  }

  // @amlalejini - TODO
  void DrawProgram() {
    if (!program_data) return;
    EM_ASM({
      var program_data_obj_id = emp.get_prog_data_obj_id();
      var svg_obj_id = emp.get_prog_vis_svg_obj_id();
      if (program_data_obj_id == 255) return; // TODO: make this more robust.
      var program_data = js.objects[program_data_obj_id][0];
      var svg = js.objects[svg_obj_id];
      var prg_name = program_data["name"];
      var iblk_h = 20;
      var iblk_w = 75;
      var fblk_w = 100;
      var iblk_lpad = fblk_w - iblk_w;
      var txt_lpad = 2;

      var vis_w = d3.select("#program-vis")[0][0].clientWidth;
      var x_domain = Array(0, 100);
      var x_range = Array(0, vis_w);
      var xScale = d3.scale.linear().domain(x_domain).range(x_range);

      // Compute program display height.
      // @amlalejini - TODO: make this a function.
      var prg_h = program_data["functions"].length * iblk_h;
      for (fID = 0; fID < program_data["functions"].length; fID++) {
        prg_h += program_data["functions"][fID].sequence_len * iblk_h;
      }
      // Reconfigure vis size based on program data.
      svg.selectAll("*").remove();
      svg.attr({"width": xScale(fblk_w), "height": prg_h});
      // Set program name.
      d3.select("#program-vis-head").text("Program: " + prg_name);
      // Add a group for each function.
      var functions = svg.selectAll("g").data(program_data["functions"]);
      functions.enter().append("g");
      functions.exit().remove();
      functions.attr({"class": "program-function",
                      "knockout": "false",
                      "transform": function(func, fID) {
                        var x_trans = xScale(0);
                        var y_trans = (fID * iblk_h + func.cumulative_seq_len * iblk_h);
                        return "translate(" + x_trans + "," + y_trans + ")";
                      }});
      var min_fsize = -1;
      functions.selectAll(".function-def-rect").remove();
      functions.selectAll(".function-def-txt").remove();
      functions.append("rect")
               .attr({
                 "class": "function-def-rect",
                 "width": xScale(fblk_w),
                 "height": iblk_h,
                 "knockout": "false"
               })
               .on("click", on_func_click);
      functions.append("text")
               .attr({
                 "class": "function-def-txt",
                 "x": txt_lpad,
                 "y": iblk_h,
                 "pointer-events": "none"
               })
               .text(function(func, fID) { return "fn-" + fID + " " + func.affinity + ":"; })
               .style("font-size", "1px")
               .each(function(d) {
                 var box = this.getBBox();
                 var pbox = this.parentNode.getBBox();
                 var fsize = Math.min(pbox.width/box.width, pbox.height/box.height)*0.9;
                 if (min_fsize == -1 || fsize < min_fsize) {
                   min_fsize = fsize;
                 }
               })
               .style("font-size", min_fsize)
               .each(function(d) {
                 var box = this.getBBox();
                 var pbox = this.parentNode.getBBox();
                 d.shift = ((pbox.height - box.height)/2);
               })
               .attr({
                 "dy": function(d) { return (-1 * (d.shift + 2)) + "px"; }
               });
      // Draw instructions for each function.
      functions.each(function(func, fID) {
        var instructions = d3.select(this).selectAll("g").data(func.sequence);
        instructions.enter().append("g");
        instructions.exit().remove();
        instructions.attr({
                          "class": "program-instruction",
                          "transform": function(d, i) {
                            var x_trans = xScale(iblk_lpad);
                            var y_trans = (iblk_h + i * iblk_h);
                            return "translate(" + x_trans + "," + y_trans + ")";
                          }});
        instructions.append("rect")
                    .attr({
                      "width": function(d) { d.w = xScale(iblk_w); return d.w; },
                      "height": iblk_h,
                      "knockout": "false"
                    })
                    .on("click", on_inst_click);
        var min_fsize = -1;
        instructions.append("text")
                    .attr({
                      "x": txt_lpad,
                      "y": iblk_h,
                      "pointer-events": "none"
                    })
                    .text(function(d, i) {
                      var inst_str = d.name;
                      if (d.has_affinity) inst_str += " " + d.affinity;
                      for (arg = 0; arg < d.num_args; arg++) inst_str += " " + d.args[arg];
                      return inst_str;
                    })
                    .style("font-size", "1px")
                    .each(function(d) {
                      var box = this.getBBox();
                      var pbox = this.parentNode.getBBox();
                      var fsize = Math.min(pbox.width/box.width, pbox.height/box.height)*0.9;
                      if (min_fsize == -1 || fsize < min_fsize) {
                        min_fsize = fsize;
                      }
                    })
                    .style("font-size", min_fsize)
                    .each(function(d) {
                      var box = this.getBBox();
                      var pbox = this.parentNode.getBBox();
                      d.shift = ((pbox.height - box.height)/2);
                    })
                    .attr({
                      "dy": function(d) { return (-1 * (d.shift + 2)) + "px"; }
                    });
      });

    });
  }

public:
  // Program data: [{
  //  name: "prgm_name",
  //  functions: [
  //    {"affinity": "0000",
  //     "sequence": [{"name": "inst_name", "has_affinity": bool, "affinity": "0000", "args": [...]}, {}, {}, ...]
  //    },
  //    {...},
  //    ...
  //   ]
  // }
  // ]
  //
  Ptr<D3::JSONDataset> program_data;

  EventDrivenGP_ProgramVis(int width, int height) : D3Visualization(width, height) {

  }

  void Setup() {
    std::cout << "LSVis setup getting run." << std::endl;
    InitializeVariables();
    this->init = true;
    this->pending_funcs.Run();
  }

  void ResetKnockouts() {
    inst_knockouts.clear();
    func_knockouts.clear();
  }

  Ptr<D3::JSONDataset> GetDataset() { return program_data; }

  // void LoadDataFromFile(std::string filename) {
  //   if (this->init) {
  //     program_data->LoadDataFromFile(filename, [this](){ std::cout << "On load?" << std::endl; });
  //   } else {
  //     this->pending_funcs.Add([this, filename]() {
  //       program_data->LoadDataFromFile(filename, [this](){ std::cout << "On load?" << std::endl; });
  //     });
  //   }
  // }

  void BuildCurProgram() {
    emp_assert(Has(program_map, display_program));
    if (cur_program) cur_program.Delete();
    const program_t & ref_program = program_map.at(display_program);
    cur_program = NewPtr<program_t>(ref_program.inst_lib);
    // Build cur_program up, knocking out appropriate functions/instructions
    for (size_t fID = 0; fID < ref_program.GetSize(); ++fID) {
      if (func_knockouts.count(fID)) continue;
      fun_t new_fun = fun_t(ref_program[fID].affinity);
      for (size_t iID = 0; iID < ref_program[fID].GetSize(); ++iID) {
        if (inst_knockouts.count(std::make_pair<int, int>(fID, iID))) continue;
        new_fun.inst_seq.emplace_back(ref_program[fID].inst_seq[iID]);
      }
      // Add new function to program if not empty.
      if (new_fun.GetSize()) cur_program->PushFunction(new_fun);
    }
    std::cout << "Built program: " << std::endl;
    cur_program->PrintProgram();
  }

  Ptr<program_t> GetCurProgram() { return cur_program; }

  void AddProgram(const std::string & _name, const program_t & _program) {
    program_map.emplace(_name, _program);
  }

  // @amlalejini - TODO
  void Clear() {

  }

  void Start(std::string name = "") {
    std::cout << "Run" << std::endl;
    if (name == "") return;
    if (this->init) {
      DisplayProgram(name);
    } else {
      this->pending_funcs.Add([this, name]() { DisplayProgram(name); });
    }
  }
};

class EventDrivenGP_DemeVis : public D3Visualization {
public:
  struct HardwareDatum {
    EMP_BUILD_INTROSPECTIVE_TUPLE(int, role_id,
                                  int, loc
                                )
  };

private:
  double y_margin;
  double x_margin;
  double cell_size = 100;

  emp::vector<HardwareDatum> deme_data;

  void InitializeVariables() {
    std::cout << "DemeVis Init vars" << std::endl;
    EM_ASM( window.addEventListener("resize", resizeDemeVis); );
    GetSVG()->Move(0, 0);
  }


public:
  EventDrivenGP_DemeVis(int width, int height) : D3Visualization(width, height) {

  }

  void Setup() {
    std::cout << "Deme vis setup running." << std::endl;
    InitializeVariables();
    this->init = true;
    this->pending_funcs.Run();
  }

  void Start(emp::Ptr<Deme> deme) {
    std::cout << "Deme vis start" << std::endl;
    if (this->init) {
      DrawDeme(deme);
    } else {
      this->pending_funcs.Add([this, deme]() { DrawDeme(deme); });
    }
  }

  void DrawDeme(emp::Ptr<Deme> deme) {
    emp_assert(deme);
    // Resize deme data to match deme size.
    deme_data.resize(deme->grid.size());
    for (size_t i = 0; i < deme_data.size(); ++i) {
      deme_data[i].role_id(deme->grid[i]->GetTrait(TRAIT_ID__ROLE_ID));
      deme_data[i].loc(i);
    }
    D3::Selection * svg = GetSVG();
    svg->SelectAll("g").Remove(); // Clean up old deme elements.
    svg->SelectAll("g").Data(deme_data)
                       .EnterAppend("g")
                       .SetAttr("class", "deme_cell");
    EM_ASM_ARGS({
      var svg = js.objects[$0];

      var deme_width = $1;
      var deme_height = $2;
      var txt_lpad = 2;

      //var cell_size = $3;

      var vis_w = d3.select("#deme-vis")[0][0].clientWidth;
      svg.attr({"deme-width": deme_width, "deme-height": deme_height, "width": vis_w, "height": vis_w});

      var cell_size = vis_w / deme_width;

      var cells = svg.selectAll("g");
      cells.attr({"transform": function(d, i) {
                      var x_trans = cell_size * (d.loc % deme_width);
                      var y_trans = cell_size * Math.floor(d.loc / deme_width);
                      return "translate(" + x_trans + "," + y_trans + ")";
                    }
                  });
      cells.append("rect")
           .attr({
             "width": cell_size,
             "height": cell_size,
             "fill": "white",
             "stroke": "black"
           });
      var min_fsize = -1;
      cells.append("text")
            .attr({"y": cell_size,
                   "x": txt_lpad,
                   "dy": "0.5em",
                   "pointer-events": "none"
                 })
            .text(function(d, i) { return "ID::" + d.role_id;})
            .style("font-size", "1px")
            .each(function(d) {
              var box = this.getBBox();
              var pbox = this.parentNode.getBBox();
              var fsize = Math.min(pbox.width/box.width, pbox.height/box.height)*0.9;
              if (min_fsize == -1 || fsize < min_fsize) {
                min_fsize = fsize;
              }
            })
            .style("font-size", min_fsize)
            .each(function(d) {
              var box = this.getBBox();
              var pbox = this.parentNode.getBBox();
              d.shift = ((pbox.height - box.height)/2);
            })
            .attr({"dy": function(d) { return (-1 * (d.shift + 2)) + "px"; }});

    }, svg->GetID(), deme->GetWidth(), deme->GetHeight(), cell_size);

  }

};

}
}

namespace web = emp::web;

class Application {
private:
  emp::Random *random;

  // Localized parameter values.
  // -- General --
  int random_seed;
  size_t deme_width;
  size_t deme_height;
  size_t deme_size;
  size_t deme_eval_time;
  size_t cur_time;

  // Interface-specific objects.
  web::EventDrivenGP_ProgramVis program_vis;
  web::EventDrivenGP_DemeVis deme_vis;

  web::Document program_vis_doc;
  web::Document deme_vis_doc;
  web::Document vis_dash;

  // Animation
  web::Animate anim;

  // Simulation/evaluation objects.
  emp::Ptr<Deme> eval_deme;
  emp::Ptr<Agent> eval_agent;

public:
  Application()
    : random(),
      program_vis(1, 1),
      deme_vis(1, 1),
      program_vis_doc("program-vis"),
      deme_vis_doc("deme-vis"),
      vis_dash("vis-dashboard"),
      anim([this]() { Application::Animate(anim); } ),
      eval_deme(),
      eval_agent()
  {

    // Localize parameter values.
    random_seed = DEFAULT_RANDOM_SEED;
    deme_width = DIST_SYS_WIDTH;
    deme_height = DIST_SYS_HEIGHT;
    deme_size = deme_width * deme_height;
    deme_eval_time = EVAL_TIME;
    cur_time = 0;
    // Create random number generator.
    random = emp::NewPtr<emp::Random>(random_seed);
    // Confiigure instruction set/event library.
    emp::Ptr<event_lib_t> event_lib = emp::NewPtr<event_lib_t>(*emp::EventDrivenGP::DefaultEventLib());
    emp::Ptr<inst_lib_t> inst_lib = emp::NewPtr<inst_lib_t>(*emp::EventDrivenGP::DefaultInstLib());
    inst_lib->AddInst("GetRoleID", Inst_GetRoleID, 1, "Local memory[Arg1] = Trait[RoleID]");
    inst_lib->AddInst("SetRoleID", Inst_SetRoleID, 1, "Trait[RoleID] = Local memory[Arg1]");
    inst_lib->AddInst("GetXLoc", Inst_GetXLoc, 1, "Local memory[Arg1] = Trait[XLoc]");
    inst_lib->AddInst("GetYLoc", Inst_GetYLoc, 1, "Local memory[Arg1] = Trait[YLoc]");

    // Configure evaluation deme.
    eval_deme = emp::NewPtr<Deme>(random, deme_width, deme_height, event_lib, inst_lib);

    // Add program visualization to page.
    program_vis_doc << program_vis;
    deme_vis_doc << deme_vis;
    // Add dashboard components to page.
    vis_dash << "<div class='row'>"
             << web::Button([this]() { this->RunCurProgram(); }, "Run", "run_program_but")
             << "</div>";
    vis_dash << "<div class='row justify-content-center pad-top-row'>"
             << "<div class='col'>"
             << "<h3>Update: <span class=\"badge badge-default\">" << web::Live([this]() { return this->cur_time; }) << "</span></h3>"
             << "</div>"
             << "</div>";
    auto run_button = vis_dash.Button("run_program_but");
    run_button.SetAttr("class", "btn btn-primary");

    // Configure program visualization.
    // Test program.
    program_t test_program(inst_lib);
    test_program.PushFunction(fun_t());
    for (size_t i=0; i < 5; ++i) test_program.PushInst("Nop");
    test_program.PushInst("GetXLoc", 0);
    test_program.PushInst("SetRoleID", 0);
    test_program.PushFunction(fun_t());
    for (size_t i=0; i < 5; ++i) test_program.PushInst("Nop");
    test_program.PushInst("Call", 0, 0, 0, affinity_t());
    test_program.PushInst("Inc", 1);
    test_program.PushInst("Add", 0, 1, 5);
    program_vis.AddProgram("Test", test_program);
    //

    // Start the visualization.
    program_vis.Start("Test");
    program_vis.On("resize", [this]() { std::cout << "On program vis resize!" << std::endl; });
    // Configure deme visualization.
    deme_vis.Start(eval_deme);
  }

  void DoFinishEval() {
    std::cout << "Finish eval" << std::endl;
    anim.Stop();
    eval_deme->Print();
  }

  void Animate(const web::Animate & anim) {
    std::cout << "Cur time: " << cur_time << std::endl;
    if (cur_time >= deme_eval_time) {
      DoFinishEval();
      return;
    }
    eval_deme->SingleAdvance();
    ++cur_time;
    vis_dash.Redraw();
    deme_vis.DrawDeme(eval_deme);
  }

  void RunCurProgram() {
    std::cout << "Run cur program!" << std::endl;
    if (anim.GetActive()) anim.Stop();
    // Build current program.
    program_vis.BuildCurProgram();
    emp::Ptr<program_t> cur_prog = program_vis.GetCurProgram();
    if (cur_prog->GetSize() == 0) {
      std::cout << "Warning! Empty program!" << std::endl;
      return;
    }
    // Configure eval agent.
    if (eval_agent) eval_agent.Delete();
    eval_agent = emp::NewPtr<Agent>(*cur_prog);
    cur_time = 0;
    // Load eval agent into deme.
    eval_deme->LoadAgent(eval_agent);
    // Evaluate deme.
    anim.Start();
  }

};

emp::Ptr<Application> app;

int main(int argc, char *argv[]) {
  emp::Initialize();
  app = emp::NewPtr<Application>();
  std::cout << "Right below app init." << std::endl;
  return 0;
}
