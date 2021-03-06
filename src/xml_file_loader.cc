// xml_file_loader.cc
// Implements file reader for an XML format
#include <algorithm>
#include <fstream>
#include <set>
#include <streambuf>

#include <boost/filesystem.hpp>

#include "blob.h"
#include "context.h"
#include "cyc_std.h"
#include "env.h"
#include "error.h"
#include "greedy_preconditioner.h"
#include "greedy_solver.h"
#include "logger.h"
#include "agent.h"
#include "infile_tree.h"
#include "sim_init.h"

#include "xml_file_loader.h"

namespace cyclus {

namespace fs = boost::filesystem;

void LoadStringstreamFromFile(std::stringstream& stream,
                              std::string file) {
  std::ifstream file_stream(file.c_str());
  if (!file_stream) {
    throw IOError("The file '" + file + "' could not be loaded.");
  }

  stream << file_stream.rdbuf();
  file_stream.close();
}

std::string BuildMasterSchema(std::string schema_path) {
  Timer ti;
  Recorder rec;
  Context ctx(&ti, &rec);

  std::stringstream schema("");
  LoadStringstreamFromFile(schema, schema_path);
  std::string master = schema.str();

  std::vector<std::string> names = Env::ListModules();
  std::map<std::string, std::string> subschemas;
  for (int i = 0; i < names.size(); ++i) {
    Agent* m = DynamicModule::Make(&ctx, names[i]);
    subschemas[m->kind()] += "<element name=\"" + names[i] + "\">\n";
    subschemas[m->kind()] += m->schema() + "\n";
    subschemas[m->kind()] += "</element>\n";
    ctx.DelAgent(m);
  }

  // replace refs in master rng template file
  std::map<std::string, std::string>::iterator it;
  for (it = subschemas.begin(); it != subschemas.end(); ++it) {
    std::string search_str = std::string("@") + it->first + std::string("_REFS@");
    size_t pos = master.find(search_str);
    if (pos != std::string::npos) {
      master.replace(pos, search_str.size(), it->second);
    }
  }

  return master;
}

Composition::Ptr ReadRecipe(InfileTree* qe) {
  bool atom_basis;
  std::string basis_str = qe->GetString("basis");
  if (basis_str == "atom") {
    atom_basis = true;
  } else if (basis_str == "mass") {
    atom_basis = false;
  } else {
    throw IOError(basis_str + " basis is not 'mass' or 'atom'.");
  }

  double value;
  int key;
  std::string query = "nuclide";
  int nnucs = qe->NMatches(query);
  CompMap v;
  for (int i = 0; i < nnucs; i++) {
    InfileTree* nuclide = qe->SubTree(query, i);
    key = strtol(nuclide->GetString("id").c_str(), NULL, 10);
    value = strtod(nuclide->GetString("comp").c_str(), NULL);
    v[key] = value;
    CLOG(LEV_DEBUG3) << "  Nuclide: " << key << " Value: " << v[key];
  }

  if (atom_basis) {
    return Composition::CreateFromAtom(v);
  } else {
    return Composition::CreateFromMass(v);
  }
}

XMLFileLoader::XMLFileLoader(Recorder* r,
                             QueryableBackend* b,
                             std::string schema_file,
                             const std::string input_file) : b_(b), rec_(r) {
  ctx_ = new Context(&ti_, rec_);

  schema_path_ = schema_file;
  file_ = input_file;
  std::stringstream input;
  LoadStringstreamFromFile(input, file_);
  parser_ = boost::shared_ptr<XMLParser>(new XMLParser());
  parser_->Init(input);

  ctx_->NewDatum("InputFiles")
  ->AddVal("Data", Blob(input.str()))
  ->Record();
}

XMLFileLoader::~XMLFileLoader() {
  rec_->Flush();
  delete ctx_;
}

std::string XMLFileLoader::master_schema() {
  return BuildMasterSchema(schema_path_);
}

void XMLFileLoader::LoadSim() {
  std::stringstream ss(master_schema());
  parser_->Validate(ss);
  LoadControlParams(); // must be first
  LoadSolver();
  LoadRecipes();
  LoadInitialAgents(); // must be last
  SimInit::Snapshot(ctx_);
  rec_->Flush();
};

void XMLFileLoader::LoadSolver() {
  InfileTree xqe(*parser_);
  std::string query = "/*/commodity";

  std::map<std::string, double> commod_order;
  std::string name;
  double order;
  int num_commods = xqe.NMatches(query);
  for (int i = 0; i < num_commods; i++) {
    InfileTree* qe = xqe.SubTree(query, i);
    name = qe->GetString("name");
    order = OptionalQuery<double>(qe, "solution_order", -1);
    commod_order[name] = order;
  }

  ProcessCommodities(&commod_order);
  std::map<std::string, double>::iterator it;
  for (it = commod_order.begin(); it != commod_order.end(); ++it) {
    ctx_->NewDatum("CommodPriority")
      ->AddVal("Commodity", it->first)
      ->AddVal("SolutionOrder", it->second)
      ->Record();
  }
}

void XMLFileLoader::ProcessCommodities(
  std::map<std::string, double>* commodity_order) {
  double max = std::max_element(
                 commodity_order->begin(),
                 commodity_order->end(),
                 SecondLT< std::pair<std::string, double> >())->second;
  if (max < 1) {
    max = 0;  // in case no orders are specified
  }

  std::map<std::string, double>::iterator it;
  for (it = commodity_order->begin();
       it != commodity_order->end();
       ++it) {
    if (it->second < 1) {
      it->second = max + 1;
    }
    CLOG(LEV_INFO1) << "Commodity ordering for " << it->first
                    << " is " << it->second;
  }
}

void XMLFileLoader::LoadRecipes() {
  InfileTree xqe(*parser_);

  std::string query = "/*/recipe";
  int num_recipes = xqe.NMatches(query);
  for (int i = 0; i < num_recipes; i++) {
    InfileTree* qe = xqe.SubTree(query, i);
    std::string name = qe->GetString("name");
    CLOG(LEV_DEBUG3) << "loading recipe: " << name;
    Composition::Ptr comp = ReadRecipe(qe);
    comp->Record(ctx_);
    ctx_->AddRecipe(name, comp);
  }
}

void XMLFileLoader::LoadInitialAgents() {
  std::map<std::string, std::string> schema_paths;
  schema_paths["Region"] = "/*/region";
  schema_paths["Inst"] = "/*/region/institution";
  schema_paths["Facility"] = "/*/facility";

  std::set<std::string> module_types;
  module_types.insert("Region");
  module_types.insert("Inst");
  module_types.insert("Facility");
  std::set<std::string>::iterator it;
  InfileTree xqe(*parser_);

  // create prototypes
  std::string prototype; // defined here for force-create AgentExit tbl
  for (it = module_types.begin(); it != module_types.end(); it++) {
    int num_agents = xqe.NMatches(schema_paths[*it]);
    for (int i = 0; i < num_agents; i++) {
      InfileTree* qe = xqe.SubTree(schema_paths[*it], i);
      InfileTree* module_data = qe->SubTree("agent");
      std::string module_name = module_data->GetElementName();
      prototype = qe->GetString("name");

      Agent* agent = DynamicModule::Make(ctx_, module_name);
      agent->set_agent_impl(module_name);

      // call manually without agent impl injected to keep all Agent state in a
      // single, consolidated db table
      agent->Agent::InfileToDb(qe, DbInit(agent, true));

      agent->InfileToDb(qe, DbInit(agent));
      rec_->Flush();

      std::vector<Cond> conds;
      conds.push_back(Cond("SimId", "==", rec_->sim_id()));
      conds.push_back(Cond("SimTime", "==", static_cast<int>(0)));
      conds.push_back(Cond("AgentId", "==", agent->id()));
      CondInjector ci(b_, conds);
      PrefixInjector pi(&ci, "AgentState");

      // call manually without agent impl injected
      agent->Agent::InitFrom(&pi);

      pi = PrefixInjector(&ci, "AgentState" + module_name);
      agent->InitFrom(&pi);
      ctx_->AddPrototype(prototype, agent);
    }
  }

  // build initial agent instances
  int nregions = xqe.NMatches(schema_paths["Region"]);
  for (int i = 0; i < nregions; ++i) {
    InfileTree* qe = xqe.SubTree(schema_paths["Region"], i);
    std::string region_proto = qe->GetString("name");
    Agent* reg = BuildAgent(region_proto, NULL);

    int ninsts = qe->NMatches("institution");
    for (int j = 0; j < ninsts; ++j) {
      InfileTree* qe2 = qe->SubTree("institution", j);
      std::string inst_proto = qe2->GetString("name");
      Agent* inst = BuildAgent(inst_proto, reg);

      int nfac = qe2->NMatches("initialfacilitylist/entry");
      for (int k = 0; k < nfac; ++k) {
        InfileTree* qe3 = qe2->SubTree("initialfacilitylist/entry", k);
        std::string fac_proto = qe3->GetString("prototype");

        int number = atoi(qe3->GetString("number").c_str());
        for (int z = 0; z < number; ++z) {
          Agent* fac = BuildAgent(fac_proto, inst);
        }
      }
    }
  }
}

Agent* XMLFileLoader::BuildAgent(std::string proto, Agent* parent) {
  Agent* m = ctx_->CreateAgent<Agent>(proto);
  m->Build(parent);
  if (parent != NULL) {
    parent->BuildNotify(m);
  }
  return m;
}

void XMLFileLoader::LoadControlParams() {
  InfileTree xqe(*parser_);
  std::string query = "/*/control";
  InfileTree* qe = xqe.SubTree(query);

  std::string handle;
  if (qe->NMatches("simhandle") > 0) {
    handle = qe->GetString("simhandle");
  }

  // get duration
  std::string dur_str = qe->GetString("duration");
  int dur = strtol(dur_str.c_str(), NULL, 10);
  // get start month
  std::string m0_str = qe->GetString("startmonth");
  int m0 = strtol(m0_str.c_str(), NULL, 10);
  // get start year
  std::string y0_str = qe->GetString("startyear");
  int y0 = strtol(y0_str.c_str(), NULL, 10);
  // get decay interval
  std::string decay_str = qe->GetString("decay");
  int dec = strtol(decay_str.c_str(), NULL, 10);

  ctx_->InitSim(SimInfo(dur, y0, m0, dec, handle));
}

} // namespace cyclus

