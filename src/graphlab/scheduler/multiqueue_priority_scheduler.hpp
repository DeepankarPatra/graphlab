/**  
 * Copyright (c) 2009 Carnegie Mellon University. 
 *     All rights reserved.
 *
 *  Licensed under the Apache License, Version 2.0 (the "License");
 *  you may not use this file except in compliance with the License.
 *  You may obtain a copy of the License at
 *
 *      http://www.apache.org/licenses/LICENSE-2.0
 *
 *  Unless required by applicable law or agreed to in writing,
 *  software distributed under the License is distributed on an "AS
 *  IS" BASIS, WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either
 *  express or implied.  See the License for the specific language
 *  governing permissions and limitations under the License.
 *
 * For more about this software visit:
 *
 *      http://www.graphlab.ml.cmu.edu
 *
 */


/**
 * \author jegonzal This class defines a multiqueue version of the
 * priority scheduler.
 **/
#ifndef GRAPHLAB_MULTIQUEUE_PRIORITY_SCHEDULER_HPP
#define GRAPHLAB_MULTIQUEUE_PRIORITY_SCHEDULER_HPP

#include <queue>
#include <cmath>
#include <cassert>

#include <graphlab/parallel/pthread_tools.hpp>

#include <graphlab/util/mutable_queue.hpp>
#include <graphlab/scheduler/ischeduler.hpp>
#include <graphlab/scheduler/terminator/iterminator.hpp>
#include <graphlab/scheduler/vertex_functor_set.hpp>

#include <graphlab/scheduler/terminator/critical_termination.hpp>
#include <graphlab/options/options_map.hpp>


#include <graphlab/macros_def.hpp>
namespace graphlab {

   /** \ingroup group_schedulers
    */
  template<typename Engine>
  class multiqueue_priority_scheduler : public ischeduler<Engine> {  
  public:

    typedef ischeduler<Engine> base;
    typedef typename base::graph_type graph_type;
    typedef typename base::engine_type engine_type;
    typedef typename base::vertex_id_type vertex_id_type;
    typedef typename base::update_functor_type update_functor_type;


    typedef mutable_queue<vertex_id_type, double> queue_type;

  private:
    vertex_functor_set<engine_type> vfun_set;
    std::vector<queue_type> queues;
    std::vector<spinlock>   locks;
    size_t queues_per_thread;
    std::vector<size_t>     current_queue;

    /** Max priority */
    double min_priority;

    // Terminator
    critical_termination term;
 


  public:

    multiqueue_priority_scheduler(const graph_type& graph, 
                                  size_t ncpus,
                                  const options_map& opts) :
      vfun_set(graph.num_vertices()), queues_per_thread(3),
      current_queue(ncpus), min_priority(-std::numeric_limits<double>::max()),
      term(ncpus) {     
      opts.get_double_option("min_priority", min_priority);
      opts.get_int_option("mult", queues_per_thread);
      const size_t nqueues = queues_per_thread*ncpus;
      queues.resize(nqueues);
      locks.resize(nqueues);
    }

    void start() { term.reset(); }
   

    void schedule(const size_t cpuid,
                  const vertex_id_type vid, 
                  const update_functor_type& fun) {      
      const size_t idx = vid % queues.size();
      double priority = 0;
      locks[idx].lock(); 
      if (vfun_set.add(vid, fun, priority)) {
        term.new_job(cpuid);
        queues[idx].push(vid, priority); 
      } else { queues[idx].update(vid, priority); }
      locks[idx].unlock();
    } // end of schedule

    void schedule_all(const update_functor_type& fun) {
      for (vertex_id_type vid = 0; vid < vfun_set.size(); ++vid) 
        schedule( vid % current_queue.size(), vid, fun);
    } // end of schedule_all

    void completed(const size_t cpuid,
                   const vertex_id_type vid,
                   const update_functor_type& fun) { term.completed_job(); }


    /** Get the next element in the queue */
    sched_status::status_enum get_next(const size_t cpuid,
                                       vertex_id_type& ret_vid,
                                       update_functor_type& ret_fun) {
      /* Check all of my queues for a task */
      for(size_t i = 0; i < queues_per_thread; ++i) {
        const size_t idx = (++current_queue[cpuid] % queues_per_thread) + 
          cpuid * queues_per_thread;
        locks[idx].lock();
        if(!queues[idx].empty() && 
           queues[idx].top().second >= min_priority) {
          ret_vid = queues[idx].pop().first;
          const bool get_success = vfun_set.test_and_get(ret_vid, ret_fun);
          ASSERT_TRUE(get_success);
          locks[idx].unlock();
          return sched_status::NEW_TASK;          
        }
        locks[idx].unlock();
      }
      /* Check all the queues */
      for(size_t i = 0; i < queues.size(); ++i) {
        const size_t idx = ++current_queue[cpuid] % queues.size();
        if(!queues[idx].empty()) { // quick pretest
          locks[idx].lock();
          if(!queues[idx].empty() && 
             queues[idx].top().second >= min_priority) {
            ret_vid = queues[idx].pop().first;
            const bool get_success = vfun_set.test_and_get(ret_vid, ret_fun);
            ASSERT_TRUE(get_success);
            locks[idx].unlock();
            return sched_status::NEW_TASK;          
          }
          locks[idx].unlock();
        }
      }
      return sched_status::EMPTY;     
    } // end of get_next_task

    iterminator& terminator() { return term; }

    static void print_options_help(std::ostream& out) { 
      out << "\t mult=3: number of queues per thread.\n" 
          << "\t min_priority=-infty Minimum priority required "
          << "\t    to run the update functor" << std::endl;
    }

  }; 


} // end of namespace graphlab
#include <graphlab/macros_undef.hpp>

#endif
