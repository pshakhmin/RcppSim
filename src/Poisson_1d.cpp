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

#include "interpolation.h"

using namespace std;
using namespace alglib;

#ifndef POISSON_1D_H
#define POISSON_1D_H

struct Cell_1d
{
  
  vector<double> coords_x;
  vector<double> death_rates;
  
  Cell_1d() {}
};


struct Grid_1d
{
  std::vector<Cell_1d> cells;
  std::vector<double> cell_death_rates;
  std::vector<int> cell_population;
  
  double area_length_x;
  
  int cell_count_x;
  
  double b, d, dd;
  int seed;
  boost::random::lagged_fibonacci2281 rng;
  
  double initial_density;
  
  int total_population;
  double total_death_rate;
  
  double time;
  int event_count;
  
  std::vector<double> death_kernel_x;
  std::vector<double> death_kernel_y;
  
  std::vector<double> birth_kernel_x;
  std::vector<double> birth_kernel_y;
  
  double spline_precision;
  
  double death_cutoff_r;
  double birth_cutoff_r;
  
  int death_spline_nodes;
  int birth_spline_nodes;
  
  spline1dinterpolant death_kernel_spline;
  spline1dinterpolant birth_kernel_spline;
  
  spline1dinterpolant birth_reverse_cdf_spline;
  
  int cull_x;
  
  std::tuple<double, int> last_event;
  
  Cell_1d& cell_at(int i)
  {
    return cells[i];
  }
  
  double & cell_death_rate_at(int i)
  {
    return cell_death_rates[i];
  }
  
  int& cell_population_at(int i)
  {
    return cell_population[i];
  }
  
  vector<double> get_coords_at_cell(int i){
    return cells[i].coords_x;
  }
  
  vector<double> get_death_rates_at_cell(int i){
    return cells[i].death_rates;
  }
  
  vector<double> get_all_coords(){
    vector<double> result;
    for(auto cell :cells){
      if(cell.coords_x.size()!=0)
        result.insert(result.end(),cell.coords_x.begin(),cell.coords_x.end());
    }
    return result;
  }
  
  vector<double> get_all_death_rates(){
    vector<double> result;
    for(auto cell :cells){
      if(cell.coords_x.size()!=0)
        result.insert(result.end(),cell.death_rates.begin(),cell.death_rates.end());
    }
    return result;
  }
  
  
  void Initialize_death_rates()
  {
    
    for(int i=0;i<cell_count_x;i++){
        cells.push_back(Cell_1d());
        cell_death_rates.push_back(0);
        cell_population.push_back(0);
    }
    
    
    //Spawn all speciments
    total_population = static_cast<int>(ceil(area_length_x*initial_density)); //initial population at t=0
    

    {
      double x_coord;
      int i;
      for (int k = 0; k < total_population; k++)
      {
        x_coord = boost::uniform_real<>(0, area_length_x)(rng);
        
        i = static_cast<int>(floor(x_coord*cell_count_x / area_length_x));
        
        if (i == cell_count_x) i--;
        
        cell_at(i).coords_x.push_back(x_coord);
        
        cell_at(i).death_rates.push_back(d);
        cell_death_rate_at(i) += d;
        total_death_rate += d;
        
        cell_population_at(i)++;
      }
    }
    
    for (int i = 0; i < cell_count_x; i++)
    {
      for (int k = 0; k < cell_population_at(i); k++)
      {
        for (int n = max(0, i - cull_x); n < min(cell_count_x, i + cull_x + 1); n++)
        {
          for (int p = 0; p < cell_population_at(n); p++)
          {
            if (i == n && k == p) continue; // same speciment
            
            //Distance between k-th speciment in (i) cell and p-th speciment in (n) cell
            
            double distance = abs(cell_at(i).coords_x[k] - cell_at(n).coords_x[p]);
            
            if (distance > death_cutoff_r) continue; //Too far to interact
            
            double interaction = dd*spline1dcalc(death_kernel_spline, distance);
            
            cell_at(i).death_rates[k] += interaction;
            cell_death_rate_at(i) += interaction;
            total_death_rate += interaction;
          }
        }
      }
    }
  }
  
  
  void kill_random()
  {
    
    int cell_death_index = boost::random::discrete_distribution<>(cell_death_rates)(rng);
    int in_cell_death_index = boost::random::discrete_distribution<>(cells[cell_death_index].death_rates)(rng);
    
    Cell_1d & death_cell = cells[cell_death_index];
    
    last_event = make_tuple<>(death_cell.coords_x[in_cell_death_index], -1);
    
    int cell_death_x = cell_death_index;
    
    for (int i = max(0, cell_death_x - cull_x); i < min(cell_count_x, cell_death_x + cull_x + 1); i++)
    {
      for (int k = 0; k < cell_population_at(i); k++)
      {
        if (i == cell_death_x && k == in_cell_death_index) continue;
        
        double distance = abs(cell_at(i).coords_x[k] - death_cell.coords_x[in_cell_death_index]);
        
        if (distance > death_cutoff_r) continue; //Too far to interact
        
        double interaction = dd*spline1dcalc(death_kernel_spline, distance);
        
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
    
    if (abs(cell_death_rates[cell_death_index])<1e-10)
    {
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
  
  
  void spawn_random()
  {
    int cell_index = boost::random::discrete_distribution<>(cell_population)(rng);
    
    int event_index = boost::random::uniform_smallint<>(0, cell_population[cell_index] - 1)(rng);
    
    Cell_1d & parent_cell = cells[cell_index];
    
    double x_coord_new = parent_cell.coords_x[event_index] +
      spline1dcalc(birth_reverse_cdf_spline, boost::random::uniform_01<>()(rng))*(boost::random::bernoulli_distribution<>(0.5)(rng) * 2 - 1);
    
    
    
    if (x_coord_new<0 || x_coord_new>area_length_x)
    {
      last_event = make_tuple<>(x_coord_new, 0);
      //Speciment failed to spawn and died outside area boundaries
    }
    else
    {
      last_event = make_tuple<>(x_coord_new, 1);
      
      int new_i = static_cast<int>(floor(x_coord_new*cell_count_x / area_length_x));
      
      if (new_i == cell_count_x) new_i--;
      
      //New speciment is added to the end of vector
      
      cell_at(new_i).coords_x.push_back(x_coord_new);
      cell_at(new_i).death_rates.push_back(d);
      
      cell_death_rate_at(new_i) += d;
      total_death_rate += d;
      
      cell_population_at(new_i)++;
      total_population++;
      
      for (int i = max(0, new_i - cull_x); i < min(cell_count_x, new_i + cull_x + 1); i++)
      {
        for (int k = 0; k < cell_population_at(i); k++)
        {
          if (i == new_i && k == cell_population_at(new_i) - 1) continue;
          
          double distance = abs(cell_at(i).coords_x[k] - x_coord_new);
          
          if (distance > death_cutoff_r) continue; //Too far to interact
          
          double interaction = dd*spline1dcalc(death_kernel_spline, distance);
          
          cell_at(i).death_rates[k] += interaction;
          cell_at(new_i).death_rates[cell_population_at(new_i) - 1] += interaction;
          
          cell_death_rate_at(i) += interaction;
          cell_death_rate_at(new_i) += interaction;
          
          total_death_rate += 2 * interaction;
        }
      }
    }
    
  }
  
  
  void make_event()
  {
    if(total_population==0) return;
    event_count++;
    time += boost::random::exponential_distribution<>(total_population*b + total_death_rate)(rng);
    //Rolling event according to global birth \ death rate
    if (boost::random::bernoulli_distribution<>(total_population*b / (total_population*b + total_death_rate))(rng) == 0)
    {
      kill_random();
    }
    else 
    {
      spawn_random();
    }
    
  }
  
  
  
  double get_birth_spline_value(double at){
    return spline1dcalc(birth_kernel_spline, at);  
  }
  
  double get_death_spline_value(double at){
    return spline1dcalc(death_kernel_spline, at);  
  }
  
  double get_birth_reverse_cdf_spline_value(double at){
    return spline1dcalc(birth_reverse_cdf_spline, at);  
  }
  
  void build_spline(const vector<double> &kernel_x,const vector<double> &kernel_y, spline1dinterpolant &spline){
    
    real_1d_array x_1d_array;
    real_1d_array y_1d_array;
    
    int spline_nodes=kernel_x.size();
    
    x_1d_array.setlength(spline_nodes);
    y_1d_array.setlength(spline_nodes);
    
    for (int i = 0; i < spline_nodes; i++) {
      x_1d_array[i] = kernel_x[i];
      y_1d_array[i] = kernel_y[i];
    }
    
    spline1dbuildmonotone(x_1d_array, y_1d_array,spline);
    
  }
  
  void trim_spline(vector<double> &kernel_x,vector<double> &kernel_y, spline1dinterpolant &spline,
                  int &spline_nodes, double &cutoff_r,double precision){
    
    double C=spline1dintegrate(spline, cutoff_r); //Integration constant
    
    double C_trim=(1-precision)*C;
    
    int node_max_index=0;
    
    while(spline1dintegrate(spline, kernel_x[node_max_index])<C_trim) node_max_index++;
    
    if(node_max_index!=spline_nodes-1){
      
      kernel_x.erase(kernel_x.begin()+node_max_index+1,kernel_x.end());
      kernel_y.erase(kernel_y.begin()+node_max_index+1,kernel_y.end());
      
      spline_nodes=node_max_index+1;
      cutoff_r=kernel_x[node_max_index];
      
      build_spline(kernel_x, kernel_y,spline);
    }
    
    
  }
  Grid_1d(Rcpp::List params):cells(), cell_death_rates(), cell_population(),
    death_kernel_spline(),birth_kernel_spline(),birth_reverse_cdf_spline()
  {
    
    //Parse parameters
    
    Rcpp::Environment base("package:base"); 
    
    // Make function callable from C++
    Rcpp::Function print = base["print"];  
    
    area_length_x=Rcpp::as<double>(params["area_length_x"]);
    cell_count_x=Rcpp::as<int>(params["cell_count_x"]);
    
    b=Rcpp::as<double>(params["b"]);
    d=Rcpp::as<double>(params["d"]);
    dd=Rcpp::as<double>(params["dd"]);
    
    seed=Rcpp::as<int>(params["seed"]);
    rng=boost::random::lagged_fibonacci2281(uint32_t(seed));
    
    initial_density=Rcpp::as<double>(params["init_density"]);
    
    death_kernel_x=Rcpp::as<vector<double>>(params["death_kernel_x"]);
    death_kernel_y=Rcpp::as<vector<double>>(params["death_kernel_y"]);
    
    death_cutoff_r=death_kernel_x.back();
    death_spline_nodes=death_kernel_x.size();
    
    birth_kernel_x=Rcpp::as<vector<double>>(params["birth_kernel_x"]);
    birth_kernel_y=Rcpp::as<vector<double>>(params["birth_kernel_y"]);
    
    birth_cutoff_r=birth_kernel_x.back();
    birth_spline_nodes=birth_kernel_x.size();
    
    spline_precision=Rcpp::as<double>(params["spline_precision"]);
    
    
    //Build death spline
    
    build_spline(death_kernel_x, death_kernel_y,death_kernel_spline);
    trim_spline(death_kernel_x,death_kernel_y,death_kernel_spline,death_spline_nodes,death_cutoff_r,spline_precision);
    
    //Calculate amount of cells to check around for death interaction
    
    cull_x = max(static_cast<int>(ceil(death_cutoff_r / (area_length_x/cell_count_x))), 3);
    
    //Build birth spline
    
    build_spline(birth_kernel_x,birth_kernel_y,birth_kernel_spline);
    trim_spline(birth_kernel_x,birth_kernel_y,birth_kernel_spline,birth_spline_nodes,birth_cutoff_r,spline_precision);
    //Calculate reverse CDF for birth spline
    
    real_1d_array x_quantile_1d_array;
    real_1d_array y_quantile_1d_array;
    
    x_quantile_1d_array.setlength(birth_spline_nodes);
    y_quantile_1d_array.setlength(birth_spline_nodes);
    
    double approx_const = spline1dintegrate(birth_kernel_spline, birth_cutoff_r);
    
    using boost::math::tools::newton_raphson_iterate;

    for (int i = 0; i < birth_spline_nodes; i++) {
      x_quantile_1d_array[i] = (double)i / (birth_spline_nodes - 1);
      y_quantile_1d_array[i] =
        newton_raphson_iterate(
          [=](double y) {return make_tuple(
            spline1dintegrate(birth_kernel_spline, y) / approx_const - x_quantile_1d_array[i],
              spline1dcalc(birth_kernel_spline, y) / approx_const); },
            1e-10, 0.0, birth_cutoff_r, numeric_limits<double>::digits);
    
    }
    
    spline1dbuildmonotone(x_quantile_1d_array, y_quantile_1d_array, birth_reverse_cdf_spline);
    
    //Spawn speciments and calculate death rates
    Initialize_death_rates();
    total_death_rate = accumulate(cell_death_rates.begin(), cell_death_rates.end(), 0.0);
    
  }
};

RCPP_MODULE(poisson_1d_module){
  using namespace Rcpp;
  
  class_<Grid_1d>("poisson_1d")
    .constructor<List>()
    .field_readonly("area_length_x",&Grid_1d::area_length_x)
    .field_readonly("cell_count_x",&Grid_1d::cell_count_x)
  
    .field_readonly("b",&Grid_1d::b)
    .field_readonly("d",&Grid_1d::d)
    .field_readonly("dd",&Grid_1d::dd)
  
    .field_readonly("seed",&Grid_1d::seed)
    .field_readonly("initial_density",&Grid_1d::initial_density)
  
    .field_readonly("death_kernel_x",&Grid_1d::death_kernel_x)
    .field_readonly("death_kernel_y",&Grid_1d::death_kernel_y)
    .field_readonly("death_cutoff_r",&Grid_1d::death_cutoff_r)
    .field_readonly("death_spline_nodes",&Grid_1d::death_spline_nodes)
  
    .field_readonly("birth_kernel_x",&Grid_1d::birth_kernel_x)
    .field_readonly("birth_kernel_y",&Grid_1d::birth_kernel_y)
    .field_readonly("birth_cutoff_r",&Grid_1d::birth_cutoff_r)
    .field_readonly("birth_spline_nodes",&Grid_1d::birth_spline_nodes)
  
    .field_readonly("spline_precision",&Grid_1d::spline_precision)
    
    .field_readonly("cell_death_rates",&Grid_1d::cell_death_rates)
    .field_readonly("cell_population",&Grid_1d::cell_population)
    
    .method("get_all_coordinates",&Grid_1d::get_all_coords)
    .method("get_all_death_rates",&Grid_1d::get_all_death_rates)
    
    .method("get_x_coordinates_in_cell",&Grid_1d::get_coords_at_cell)
    .method("get_death_rates_in_cell",&Grid_1d::get_death_rates_at_cell)
    
    .method("birth_spline_at",&Grid_1d::get_birth_spline_value)
    .method("death_spline_at",&Grid_1d::get_death_spline_value)
    .method("birth_reverse_cdf_spline_at",&Grid_1d::get_birth_reverse_cdf_spline_value)
    
    .method("make_event",&Grid_1d::make_event)
  ;
}

#endif
