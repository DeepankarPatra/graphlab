#include "cvv8.hpp"
#include "pilot.hpp"
#include "templates.hpp"

using namespace graphlab;
using namespace v8;
namespace cv = cvv8;

namespace graphlab {
distributed_control* pilot_dc;
}
//////////////////////////// PILOT //////////////////////////////
// TODO: make some JSTR constants to be reused throughout

// TODO: how many distributed controls can a pilot have in his lifetime? 
pilot::pilot() : graph(*pilot_dc, opts) {}
    
void pilot::ping(){ std::cout << "pong." << std::endl; }
 
// TODO: how many graphs can a pilot have in his lifetime? 
void pilot::load_graph(const std::string &path, const std::string &format){
  graph.load_format(path, format);
  graph.finalize();
}

void pilot::load_synthetic_powerlaw(size_t powerlaw){
  graph.load_synthetic_powerlaw(powerlaw);
  graph.finalize();
}

/**
 * Takes a javascript constructor to create vertex programs.
 */
void pilot::fly(const Handle<Function> &function){
  
  js_proxy::set_ctor(function); // FIXME: should not be a static!
  omni_engine<js_proxy> engine(*pilot_dc, graph, "synchronous", opts);
  engine.signal_all(); // TODO: allow user to specify an array of vertices to signal, or all
  engine.start();

  logstream(LOG_INFO) << "done." << std::endl;

  // TODO: move to another function
  const float runtime = engine.elapsed_seconds();
  size_t update_count = engine.num_updates();
  logstream(LOG_EMPH) << "Finished Running engine in " << runtime 
            << " seconds." << std::endl
            << "Total updates: " << update_count << std::endl
            << "Efficiency: " << (double(update_count) / runtime)
            << " updates per second "
            << std::endl;

}

void pilot::transform_vertices(const Handle<Function> &function){
  js_functor::set_function(function); // FIXME: should not be static!
  graph.transform_vertices(js_functor::invoke);
}

////////////////////////////// STATIC //////////////////////////////

// TODO: fix this -- clopts really shouldn't be static:
graphlab_options pilot::opts;

// object templates for vertex and edge
templates pilot::templs;

/**
 * Adds a JS binding of the class to the given object. Throws
 * a native exception on error.
 */
void pilot::setup_bindings(const Handle<Object> &dest){
  cv::ClassCreator<pilot>::Instance().SetupBindings(dest);
}

/**
 * Saves command line options for this session.
 */
void pilot::set_clopts(const graphlab_options &clopts){ opts = clopts; }

templates &pilot::get_templates(){ return templs; }

/////////////////////////// JS_PROXY //////////////////////////////

js_proxy::js_proxy() {
  // TODO deal with multi-threaded environments
  HandleScope handle_scope;
  jsobj = Persistent<Object>::New(constructor->NewInstance());
}

js_proxy::js_proxy(const js_proxy& other){
  HandleScope handle_scope;
  this->jsobj = Persistent<Object>::New(other.jsobj);
}

js_proxy &js_proxy::operator=(const js_proxy& other){
  HandleScope handle_scope;
  if (this == &other) return *this;
  this->jsobj.Dispose();
  this->jsobj = Persistent<Object>::New(other.jsobj);
  return *this;
}

js_proxy::~js_proxy(){
  jsobj.Dispose();
}

pilot::gather_type js_proxy::gather(icontext_type& context, const vertex_type& vertex, edge_type& edge) const {
  // TODO
  HandleScope handle_scope;
  Local<Function> f = Function::Cast(*jsobj->Get(JSTR("gather")));
  Handle<Value> ret = cv::CallForwarder<2>::Call(jsobj, f, vertex, edge);
  return cv::CastFromJS<gather_type>(ret);
}

void js_proxy::apply(icontext_type& context, vertex_type& vertex, const gather_type& total){
  // TODO
  HandleScope handle_scope;
  Local<Function> f = Function::Cast(*jsobj->Get(JSTR("apply")));
  cv::CallForwarder<2>::Call(jsobj, f, vertex, total);
}

edge_dir_type js_proxy::scatter_edges(icontext_type& context, const vertex_type& vertex) const {
  // TODO
  HandleScope handle_scope;
  Local<Function> f = Function::Cast(*jsobj->Get(JSTR("scatter_edges")));
  int32_t n = cv::CastFromJS<int32_t>(cv::CallForwarder<1>::Call(jsobj, f, vertex));
  // TODO push edge_dir_type as a JS variable?
  return static_cast<edge_dir_type>(n);
}

void js_proxy::scatter(icontext_type& context, const vertex_type& vertex, edge_type& edge) const {
  // TODO
  HandleScope handle_scope;
  Local<Function> f = Function::Cast(*jsobj->Get(JSTR("scatter")));
  Handle<Value> args[] = {
    cv::CastToJS(context), cv::CastToJS(vertex), cv::CastToJS(edge)
  };
  f->Call(jsobj, 3, args);
}

/////////////////////////// STATIC ////////////////////////////////

Persistent<Function> js_proxy::constructor;

void js_proxy::set_ctor(const Handle<Function> &ctor){
  // TODO: worry about memory management (should dispose?)
  HandleScope handle_scope;
  constructor  = Persistent<Function>::New(ctor);
}

//////////////////////// JS_FUNCTOR ///////////////////////////////

// TODO: fix this
Persistent<Function> js_functor::function;

void js_functor::invoke(pilot::graph_type::vertex_type &vertex){
  cv::CallForwarder<1>::Call(function, vertex); 
}

void js_functor::set_function(const Handle<Function> &func){
  // TODO: worry about memory management
  function = Persistent<Function>::New(func);
}

namespace cvv8 {

  CVV8_TypeName_IMPL((pilot), "pilot");

  pilot *ClassCreator_Factory<pilot>::
  Create(Persistent<Object> & jsSelf, Arguments const & argv){
    typedef CtorArityDispatcher<pilotCtors> Proxy;
    pilot * b = Proxy::Call( argv );
    if(b) BMap::Insert( jsSelf, b );
    return b;
  }

  void ClassCreator_Factory<pilot>::
  Delete(pilot *obj){
    BMap::Remove(obj);
    delete obj;
  }
  
  template <>
  struct ClassCreator_SetupBindings<pilot> {
    static void Initialize(Handle<Object> const & dest) {
    
      logstream(LOG_INFO) << "== Preparing cockpit " << std::flush;
    
      ////////////////////////////////////////////////////////////
      // Bootstrap class-wrapping code...
      typedef ClassCreator<pilot> CC;
      CC & cc( CC::Instance() );
      if( cc.IsSealed() ) {
        cc.AddClassTo( TypeName<pilot>::Value, dest );
        logstream(LOG_INFO) << "== cockpit ready ==>" << std::endl; 
        return;
      }
    
      ////////////////////////////////////////////////////////////
      // Bind some member functions...
      logstream(LOG_INFO) << "== strapping pilot " << std::flush;
      cc("ping", MethodToInCa<pilot, void (), &pilot::ping>::Call)
        ("destroy", CC::DestroyObjectCallback)
        ("loadGraph", 
          MethodToInCa<pilot, void (const std::string&, const std::string&),
            &pilot::load_graph>::Call)
        ("loadSyntheticPowerlaw",
          MethodToInCa<pilot, void (size_t),
            &pilot::load_synthetic_powerlaw>::Call)
        ("transformVertices",
          MethodToInCa<pilot, void (const Handle<Function> &),
            &pilot::transform_vertices>::Call)
        ("fly",
          MethodToInCa<pilot, void (const Handle<Function> &),
            &pilot::fly>::Call);
    
      ////////////////////////////////////////////////////////////
      // Add class to the destination object...
      cc.AddClassTo( TypeName<pilot>::Value, dest );
    
      // sanity checking. This code should crash if the basic stuff is horribly wrong
      logstream(LOG_INFO) << "== running test flight " << std::flush;
      Handle<Value> vinst = cc.NewInstance (0,NULL);
      pilot * native = CastFromJS<pilot>(vinst);
      if (0 == native)
        logstream(LOG_FATAL) << "== BROKEN. Unable to instantiate test flight." << std::endl;
    
      logstream(LOG_INFO) << "== success ==>"
        << std::endl;
      CC::DestroyObject( vinst );
    
      logstream(LOG_EMPH) << "Cockpit ready." << std::endl;
    
    }
  };
  
};