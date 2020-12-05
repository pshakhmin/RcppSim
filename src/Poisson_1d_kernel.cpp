// [[Rcpp::depends(BH)]]
// [[Rcpp::plugins(cpp11)]]
#include <Rcpp.h>
#include <stdio.h>
#include <functional>
#include <list>
#include <chrono>
#include <ctime>
#include <vector>
#include <string>
#include <fstream>
#include <math.h>
#include <tuple>
#include <iomanip>
#include <boost/random.hpp>
#include <boost/random/lagged_fibonacci.hpp>
#include <boost/random/exponential_distribution.hpp>
#include <boost/math/tools/roots.hpp>
#include <boost/fusion/sequence.hpp>
#include <boost/fusion/include/sequence.hpp>
#include <boost/fusion/adapted/std_tuple.hpp>
#include <boost/math/interpolators/cubic_b_spline.hpp>
#include <boost/math/quadrature/trapezoidal.hpp>


using namespace std;

#ifndef POISSON_1D_H_KERNEL
#define POISSON_1D_H_KERNEL

struct Cell_1d_kernel {
  
  vector < double > coords_x;
  vector < double > death_rates;
  
  Cell_1d_kernel() {}
};

struct Grid_1d_kernel {
  std::vector < Cell_1d_kernel > cells;
  std::vector < double > cell_death_rates;
  std::vector < int > cell_population;
  
  double area_length_x;
  bool periodic;
  
  int cell_count_x;
  
  double b, d, dd;
  int seed;
  boost::random::lagged_fibonacci2281 rng;
  
  std::vector < double > initial_population_x;
  
  int total_population;
  
  int population_limit;
  bool pop_cap_reached;
  
  double total_death_rate;
  
  double time;
  int event_count;
  
  std::vector < double > death_kernel_y;
  std::vector < double > birth_inverse_cdf_y;
  
  double death_cutoff_r;
  
  double death_step;
  double birth_reverse_cdf_step;
  
  int death_spline_nodes;
  int birth_reverse_cdf_nodes;
  
  boost::math::cubic_b_spline < double > death_kernel_spline;
  
  boost::math::cubic_b_spline < double > birth_reverse_cdf_spline;
  
  int cull_x;
  
  std::tuple < double, int > last_event;
  
  Cell_1d_kernel & cell_at(int i) {
    if (periodic) {
      if (i < 0) i += cell_count_x;
      if (i >= cell_count_x) i -= cell_count_x;
    }
    return cells[i];
  }
  
  double & cell_death_rate_at(int i) {
    if (periodic) {
      if (i < 0) i += cell_count_x;
      if (i >= cell_count_x) i -= cell_count_x;
    }
    return cell_death_rates[i];
  }
  
  int & cell_population_at(int i) {
    if (periodic) {
      if (i < 0) i += cell_count_x;
      if (i >= cell_count_x) i -= cell_count_x;
    }
    return cell_population[i];
  }
  
  vector < double > get_coords_at_cell(int i) {
    return cells[i].coords_x;
  }
  
  vector < double > get_death_rates_at_cell(int i) {
    return cells[i].death_rates;
  }
  
  vector < double > get_all_coords() {
    vector < double > result;
    for (auto cell: cells) {
      if (cell.coords_x.size() != 0)
        result.insert(result.end(), cell.coords_x.begin(), cell.coords_x.end());
    }
    return result;
  }
  
  vector < double > get_all_death_rates() {
    vector < double > result;
    for (auto cell: cells) {
      if (cell.coords_x.size() != 0)
        result.insert(result.end(), cell.death_rates.begin(), cell.death_rates.end());
    }
    return result;
  }
  
  void Initialize_death_rates() {
    
    for (int i = 0; i < cell_count_x; i++) {
      cells.push_back(Cell_1d_kernel());
      cell_death_rates.push_back(0);
      cell_population.push_back(0);
    }
    
    //Spawn all speciments
    {
      int i;
      for (double x_coord: initial_population_x) {
        if (x_coord < 0 || x_coord > area_length_x) continue;
        
        i = static_cast < int > (floor(x_coord * cell_count_x / area_length_x));
        
        if (i == cell_count_x)
          i--;
        
        cell_at(i).coords_x.push_back(x_coord);
        
        cell_at(i).death_rates.push_back(d);
        cell_death_rate_at(i) += d;
        total_death_rate += d;
        
        cell_population_at(i) ++;
        total_population++;
      }
    }
    
    for (int i = 0; i < cell_count_x; i++) {
      for (int k = 0; k < cell_population_at(i); k++) {
        for (int n = i - cull_x; n < i + cull_x + 1; n++) {
          if (!periodic && (n < 0 || n >= cell_count_x)) continue;
          
          for (int p = 0; p < cell_population_at(n); p++) {
            if (i == n && k == p)
              continue; // same speciment
            double distance;
            
            if (periodic) {
              if (n < 0) {
                distance = abs(cell_at(i).coords_x[k] - cell_at(n).coords_x[p] + area_length_x);
              } else if (n >= cell_count_x) {
                distance = abs(cell_at(i).coords_x[k] - cell_at(n).coords_x[p] - area_length_x);
              } else {
                distance = abs(cell_at(i).coords_x[k] - cell_at(n).coords_x[p]);
              }
            } else {
              distance = abs(cell_at(i).coords_x[k] - cell_at(n).coords_x[p]);
            }
            
            if (distance > death_cutoff_r)
              continue; //Too far to interact
            
            double interaction = dd * death_kernel_spline(distance);
            
            cell_at(i).death_rates[k] += interaction;
            cell_death_rate_at(i) += interaction;
            total_death_rate += interaction;
          }
        }
      }
    }
  }
  
  void kill_random() {
    
    if (total_population == 1) {
      total_population--;
      return;
    }
    
    int cell_death_index = boost::random::discrete_distribution < > (cell_death_rates)(rng);
    int in_cell_death_index = boost::random::discrete_distribution < > (cells[cell_death_index].death_rates)(rng);
    
    Cell_1d_kernel & death_cell = cells[cell_death_index];
    
    last_event = make_tuple < > (death_cell.coords_x[in_cell_death_index], -1);
    
    int cell_death_x = cell_death_index;
    
    for (int i = cell_death_x - cull_x; i < cell_death_x + cull_x + 1; i++) {
      if (!periodic && (i < 0 || i >= cell_count_x)) continue;
      for (int k = 0; k < cell_population_at(i); k++) {
        if (i == cell_death_x && k == in_cell_death_index)
          continue;
        
        double distance;
        
        if (periodic) {
          if (i < 0) {
            distance = abs(cell_at(cell_death_x).coords_x[in_cell_death_index] -
              cell_at(i).coords_x[k] + area_length_x);
          } else if (i >= cell_count_x) {
            distance = abs(cell_at(cell_death_x).coords_x[in_cell_death_index] -
              cell_at(i).coords_x[k] - area_length_x);
          } else {
            distance = abs(cell_at(cell_death_x).coords_x[in_cell_death_index] - cell_at(i).coords_x[k]);
            
          }
        } else {
          distance = abs(cell_at(cell_death_x).coords_x[in_cell_death_index] - cell_at(i).coords_x[k]);
          
        }
        
        if (distance > death_cutoff_r) continue; //Too far to interact
        
        double interaction = dd * death_kernel_spline(distance);
        
        cell_at(i).death_rates[k] -= interaction;
        //ignore dying speciment death rates since it is to be deleted
        
        cell_death_rate_at(i) -= interaction;
        cell_death_rate_at(cell_death_x) -= interaction;
        
        total_death_rate -= 2 * interaction;
      }
    }
    //remove dead speciment
    cell_death_rates[cell_death_index] -= d;
    total_death_rate -= d;
    
    if (abs(cell_death_rates[cell_death_index]) < 1e-10) {
      cell_death_rates[cell_death_index] = 0;
    }
    
    cell_population[cell_death_index]--;
    total_population--;
    
    //swap dead and last
    death_cell.death_rates[in_cell_death_index] = death_cell.death_rates[death_cell.death_rates.size() - 1];
    death_cell.coords_x[in_cell_death_index] = death_cell.coords_x[death_cell.coords_x.size() - 1];
    
    death_cell.death_rates.erase(death_cell.death_rates.end() - 1);
    death_cell.coords_x.erase(death_cell.coords_x.end() - 1);
  }
  
  void spawn_random() {
    int cell_index = boost::random::discrete_distribution < > (cell_population)(rng);
    
    int event_index = boost::random::uniform_smallint < > (0, cell_population[cell_index] - 1)(rng);
    
    Cell_1d_kernel & parent_cell = cells[cell_index];
    
    double x_coord_new = parent_cell.coords_x[event_index] +
      birth_reverse_cdf_spline(boost::random::uniform_01 < > ()(rng)) * (boost::random::bernoulli_distribution < > (0.5)(rng) * 2 - 1);
    
    if (x_coord_new < 0 || x_coord_new > area_length_x) {
      if (!periodic) {
        last_event = make_tuple < > (x_coord_new, 0);
        //Specimen failed to spawn and died outside area boundaries
        return;
      } else {
        if (x_coord_new < 0) x_coord_new = x_coord_new + area_length_x;
        if (x_coord_new > area_length_x) x_coord_new = x_coord_new - area_length_x;
      }
    }
    
    last_event = make_tuple < > (x_coord_new, 1);
    
    int new_i = static_cast < int > (floor(x_coord_new * cell_count_x / area_length_x));
    
    if (new_i == cell_count_x)
      new_i--;
    
    //New specimen is added to the end of vector
    
    cell_at(new_i).coords_x.push_back(x_coord_new);
    cell_at(new_i).death_rates.push_back(d);
    
    cell_death_rate_at(new_i) += d;
    total_death_rate += d;
    
    cell_population_at(new_i) ++;
    total_population++;
    
    for (int i = new_i - cull_x; i < new_i + cull_x + 1; i++) {
      if (!periodic && (i < 0 || i >= cell_count_x)) continue;
      for (int k = 0; k < cell_population_at(i); k++) {
        if (i == new_i && k == cell_population_at(new_i) - 1)
          continue;
        
        double distance;
        
        if (periodic) {
          if (i < 0) {
            distance = abs(cell_at(new_i).coords_x[cell_population_at(new_i) - 1] -
              cell_at(i).coords_x[k] + area_length_x);
          } else if (i >= cell_count_x) {
            distance = abs(cell_at(new_i).coords_x[cell_population_at(new_i) - 1] -
              cell_at(i).coords_x[k] - area_length_x);
          } else {
            distance = abs(cell_at(new_i).coords_x[cell_population_at(new_i) - 1] - cell_at(i).coords_x[k]);
          }
          
        } else {
          distance = abs(cell_at(new_i).coords_x[cell_population_at(new_i) - 1] - cell_at(i).coords_x[k]);
        }
        
        if (distance > death_cutoff_r)
          continue; //Too far to interact
        
        double interaction = dd * death_kernel_spline(distance);
        
        cell_at(i).death_rates[k] += interaction;
        cell_at(new_i).death_rates[cell_population_at(new_i) - 1] += interaction;
        
        cell_death_rate_at(i) += interaction;
        cell_death_rate_at(new_i) += interaction;
        
        total_death_rate += 2 * interaction;
      }
    }
    
  }
  
  void make_event() {
    if (total_population == 0)
      return;
    event_count++;
    time += boost::random::exponential_distribution < > (total_population * b + total_death_rate)(rng);
    //Rolling event according to global birth \ death rate
    if (boost::random::bernoulli_distribution < > (total_population * b / (total_population * b + total_death_rate))(rng) == 0) {
      kill_random();
    } else {
      spawn_random();
    }
  }
  
  
  void run_events(int events) {
    if (events > 0) {
    for (int i = 0; i < events; i++) {
      if (total_population > population_limit){
        pop_cap_reached = true;
        return;
      }
      make_event();
    }
    }
  }
  
  void run_for(double time) {
    if (time > 0.0) {
    double time0 = this -> time;
    while (this -> time < time0 + time) {
      if (total_population > population_limit){
        pop_cap_reached = true;
        return;
      }
      make_event();
    }
    }
  }
  
  double get_death_spline_value(double at) {
    return death_kernel_spline(at);
  }
  
  double get_birth_reverse_cdf_spline_value(double at) {
    return birth_reverse_cdf_spline(at);
  }
  
  Grid_1d_kernel(Rcpp::List params): time(), event_count(), 
  cells(), cell_death_rates(), cell_population(),
  death_kernel_spline(), birth_reverse_cdf_spline(), 
  total_population(),population_limit(),pop_cap_reached(false) {
    
    //Parse parameters
    
    area_length_x = Rcpp::as < double > (params["area_length_x"]);
    cell_count_x = Rcpp::as < int > (params["cell_count_x"]);
    
    b = Rcpp::as < double > (params["b"]);
    d = Rcpp::as < double > (params["d"]);
    dd = Rcpp::as < double > (params["dd"]);
    
    seed = Rcpp::as < int > (params["seed"]);
    rng = boost::random::lagged_fibonacci2281(uint32_t(seed));
    
    initial_population_x = Rcpp::as < vector < double >> (params["initial_population_x"]);
    
    death_kernel_y = Rcpp::as < vector < double >> (params["death_kernel_y"]);
    death_cutoff_r = Rcpp::as < double > (params["death_kernel_r"]);
    death_spline_nodes = death_kernel_y.size();
    death_step = death_cutoff_r / (death_spline_nodes - 1);
    
    birth_inverse_cdf_y = Rcpp::as < vector < double >> (params["birth_kernel_y"]);
    birth_reverse_cdf_nodes = birth_inverse_cdf_y.size();
    birth_reverse_cdf_step = 1.0 / (birth_reverse_cdf_nodes - 1);
    
    periodic = Rcpp::as < bool > (params["periodic"]);
    population_limit = Rcpp::as < int > (params["population_limit"]);
    
    
    using boost::math::cubic_b_spline;
    //Build death spline, ensure 0 derivative at 0 (symmetric) and endpoint (expected no death interaction further)
    death_kernel_spline = cubic_b_spline < double > (death_kernel_y.begin(), death_kernel_y.end(), 0, death_step, 0, 0);
    
    //Calculate amount of cells to check around for death interaction
    
    cull_x = max(static_cast < int > (ceil(death_cutoff_r / (area_length_x / cell_count_x))), 3);
    
    //Build birth inverse CDF spline, endpoint derivatives not specified
    
    birth_reverse_cdf_spline = cubic_b_spline < double > (birth_inverse_cdf_y.begin(), birth_inverse_cdf_y.end(), 0, birth_reverse_cdf_step);
    
    //Spawn specimens and calculate death rates
    Initialize_death_rates();
    total_death_rate = accumulate(cell_death_rates.begin(), cell_death_rates.end(), 0.0);
  }
};

RCPP_MODULE(poisson_1d_kernel_module) {
  using namespace Rcpp;
  
  class_ < Grid_1d_kernel > ("poisson_1d_kernel")
    .constructor < List > ()
    .field_readonly("area_length_x", & Grid_1d_kernel::area_length_x)
    .field_readonly("cell_count_x", & Grid_1d_kernel::cell_count_x)
  
  .field_readonly("cull_x", & Grid_1d_kernel::cull_x)
  .field_readonly("periodic", & Grid_1d_kernel::periodic)
  
  .field_readonly("b", & Grid_1d_kernel::b)
  .field_readonly("d", & Grid_1d_kernel::d)
  .field_readonly("dd", & Grid_1d_kernel::dd)
  
  .field_readonly("seed", & Grid_1d_kernel::seed)
  .field_readonly("initial_population_x", & Grid_1d_kernel::initial_population_x)
  
  .field_readonly("death_kernel_y", & Grid_1d_kernel::death_kernel_y)
  .field_readonly("death_cutoff_r", & Grid_1d_kernel::death_cutoff_r)
  .field_readonly("death_spline_nodes", & Grid_1d_kernel::death_spline_nodes)
  .field_readonly("death_step", & Grid_1d_kernel::death_step)
  
  .field_readonly("birth_inverse_cdf_y", & Grid_1d_kernel::birth_inverse_cdf_y)
  .field_readonly("birth_reverse_cdf_nodes", & Grid_1d_kernel::birth_reverse_cdf_nodes)
  .field_readonly("birth_step", & Grid_1d_kernel::birth_reverse_cdf_step)
  
  .field_readonly("birth_reverse_cdf_nodes", & Grid_1d_kernel::birth_reverse_cdf_nodes)
  .field_readonly("birth_reverse_cdf_step", & Grid_1d_kernel::birth_reverse_cdf_step)
  
  .field_readonly("cell_death_rates", & Grid_1d_kernel::cell_death_rates)
  .field_readonly("cell_population", & Grid_1d_kernel::cell_population)
  
  .method("get_all_x_coordinates", & Grid_1d_kernel::get_all_coords)
  .method("get_all_death_rates", & Grid_1d_kernel::get_all_death_rates)
  
  .method("get_x_coordinates_in_cell", & Grid_1d_kernel::get_coords_at_cell)
  .method("get_death_rates_in_cell", & Grid_1d_kernel::get_death_rates_at_cell)
  
  .method("death_spline_at", & Grid_1d_kernel::get_death_spline_value)
  .method("birth_reverse_cdf_spline_at", & Grid_1d_kernel::get_birth_reverse_cdf_spline_value)
  
  .method("make_event", & Grid_1d_kernel::make_event)
  .method("run_events", & Grid_1d_kernel::run_events)
  .method("run_for", & Grid_1d_kernel::run_for)
  
  .field_readonly("total_population", & Grid_1d_kernel::total_population)
  .field_readonly("total_death_rate", & Grid_1d_kernel::total_death_rate)
  .field_readonly("events", & Grid_1d_kernel::event_count)
  .field_readonly("time", & Grid_1d_kernel::time)
  
  .field_readonly("population_limit", & Grid_1d_kernel::population_limit)
  .field_readonly("pop_cap_reached", & Grid_1d_kernel::pop_cap_reached);
}
# endif