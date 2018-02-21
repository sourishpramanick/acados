
#include <algorithm>
#include <iostream>
#include <iterator>
#include <numeric>
#include <stdexcept>
#include <cstdlib>
#include <cmath>

#include "acados_cpp/ocp_qp.hpp"

#include "acados/utils/print.h"
#include "acados_c/ocp_qp.h"
#include "acados_c/options.h"

#include "acados_cpp/hpipm_helper.hpp"
#include "acados_cpp/utils.hpp"

using std::map;
using std::string;
using std::vector;

namespace acados {

ocp_qp::ocp_qp(std::vector<uint> nx, std::vector<uint> nu, std::vector<uint> nbx,
             std::vector<uint> nbu, std::vector<uint> ng) : N(nx.size()-1), qp(nullptr), solver(nullptr) {

    if (N <= 0) throw std::invalid_argument("Number of stages must be positive");

    if (nu.at(N) != 0 || nbu.at(N) != 0) {
        nu.at(N) = 0;
        nbu.at(N) = 0;
    }

    uint expected_size = nx.size();
    bool is_valid_nu = (nu.size() == expected_size || nu.size() == expected_size-1);
    bool is_valid_nbu = (nbu.size() == expected_size || nbu.size() == expected_size-1);
    if (!is_valid_nu || nbx.size() != expected_size || !is_valid_nbu || ng.size() != expected_size)
        throw std::invalid_argument("All dimensions should have length N+1");

    auto dim = std::unique_ptr<ocp_qp_dims>(create_ocp_qp_dims(N));

    // states
    std::copy_n(std::begin(nx), N+1, dim->nx);

    // controls
    std::copy_n(std::begin(nu), N+1, dim->nu);

    // bounds
    std::copy_n(std::begin(nbx), N+1, dim->nbx);
    std::copy_n(std::begin(nbu), N+1, dim->nbu);

    std::vector<uint> nb(N+1);
    for(uint i = 0; i <= N; i++)
        nb.at(i) = nbx.at(i) + nbu.at(i);
    std::copy_n(std::begin(nb), N+1, dim->nb);

    // constraints
    std::copy_n(std::begin(ng), N+1, dim->ng);

    // slacks
    std::vector<uint> ns(N+1, 0);
    std::copy_n(std::begin(ns), N+1, dim->ns);

    qp = std::unique_ptr<ocp_qp_in>(create_ocp_qp_in(dim.get()));

    for (uint i = 0; i < N; ++i) {
        std::vector<uint> idx_states(nx.at(i));
        std::iota(std::begin(idx_states), std::end(idx_states), 0);
        set_bounds_indices("x", i, idx_states);

        std::vector<uint> idx_controls(nu.at(i));
        std::iota(std::begin(idx_controls), std::end(idx_controls), 0);        
        set_bounds_indices("u", i, idx_controls);
    }
    std::vector<uint> idx_states(nx.at(N));
    std::iota(std::begin(idx_states), std::end(idx_states), 0);
    set_bounds_indices("x", N, idx_states);

    for (uint stage = 0; stage <= N; ++stage) {
        auto lbx = vector<double>(qp->dim->nbx[stage], -INFINITY);
        set("lbx", stage, lbx);
        auto ubx = vector<double>(qp->dim->nbx[stage], +INFINITY);
        set("ubx", stage, ubx);
        auto lbu = vector<double>(qp->dim->nbu[stage], -INFINITY);
        set("lbu", stage, lbu);
        auto ubu = vector<double>(qp->dim->nbu[stage], +INFINITY);
        set("ubu", stage, ubu);
    }
}

ocp_qp::ocp_qp(map<string, vector<uint>> dims)
    : ocp_qp(dims["nx"], dims["nu"], dims["nbx"], dims["nbu"], dims["ng"]) {}

ocp_qp::ocp_qp(uint N, uint nx, uint nu, uint nbx, uint nbu, uint ng)
    : ocp_qp(std::vector<uint>(N+1, nx), std::vector<uint>(N+1, nu), std::vector<uint>(N+1, nbx),
      std::vector<uint>(N+1, nbu), std::vector<uint>(N+1, ng)) {}

void ocp_qp::set(std::string field, uint stage, std::vector<double> v) {

    check_range(field, stage);
    check_nb_elements(field, stage, v.size());

    if (!needs_initializing) {
        if (field == "lbx" || field == "ubx") {
            expand_dimensions();
            auto idxb = bounds_indices("x");
            auto sq_ids = squeezed_bounds_indices("x", stage);
            if (idxb.at(stage).size() != sq_ids.size())
                needs_initializing = true;
            for (uint idx = 0; idx < idxb.at(stage).size() && idx < sq_ids.size(); ++idx) {
                if (idxb.at(stage).at(idx) != sq_ids.at(idx)) {
                    needs_initializing = true;
                }
            }
        } else if (field == "lbu" || field == "ubu") {
            expand_dimensions();
            auto idxb = bounds_indices("u");
            auto sq_ids = squeezed_bounds_indices("u", stage);
            if (idxb.at(stage).size() != sq_ids.size())
                needs_initializing = true;
            for (uint idx = 0; idx < idxb.at(stage).size() && idx < sq_ids.size(); ++idx) {
                if (idxb.at(stage).at(idx) != sq_ids.at(idx)) {
                    needs_initializing = true;
                }
            }
        }
    }

    if (field == "Q")
        d_cvt_colmaj_to_ocp_qp_Q(stage, v.data(), qp.get());
    else if (field == "S")
        d_cvt_colmaj_to_ocp_qp_S(stage, v.data(), qp.get());
    else if (field == "R")
        d_cvt_colmaj_to_ocp_qp_R(stage, v.data(), qp.get());
    else if (field == "q")
        d_cvt_colmaj_to_ocp_qp_q(stage, v.data(), qp.get());
    else if (field == "r")
        d_cvt_colmaj_to_ocp_qp_r(stage, v.data(), qp.get());
    else if (field == "A")
        d_cvt_colmaj_to_ocp_qp_A(stage, v.data(), qp.get());
    else if (field == "B")
        d_cvt_colmaj_to_ocp_qp_B(stage, v.data(), qp.get());
    else if (field == "b")
        d_cvt_colmaj_to_ocp_qp_b(stage, v.data(), qp.get());
    else if (field == "lbx")
        d_cvt_colmaj_to_ocp_qp_lbx(stage, v.data(), qp.get());
    else if (field == "ubx")
        d_cvt_colmaj_to_ocp_qp_ubx(stage, v.data(), qp.get());
    else if (field == "lbu")
        d_cvt_colmaj_to_ocp_qp_lbu(stage, v.data(), qp.get());
    else if (field == "ubu")
        d_cvt_colmaj_to_ocp_qp_ubu(stage, v.data(), qp.get());
    else if (field == "C")
        d_cvt_colmaj_to_ocp_qp_C(stage, v.data(), qp.get());
    else if (field == "D")
        d_cvt_colmaj_to_ocp_qp_D(stage, v.data(), qp.get());
    else if (field == "lg")
        d_cvt_colmaj_to_ocp_qp_lg(stage, v.data(), qp.get());
    else if (field == "ug")
        d_cvt_colmaj_to_ocp_qp_ug(stage, v.data(), qp.get());
    else
        throw std::invalid_argument("OCP QP does not contain field " + field);
}

void ocp_qp::set(string field, vector<double> v) {
    uint last_stage = N;
    if (field == "A" || field == "B" || field == "b")
        last_stage = N-1;

    for (uint stage = 0; stage <= last_stage; ++stage)
        set(field, stage, v);
}

static ocp_qp_solver_plan string_to_plan(string solver);

void ocp_qp::initialize_solver(string solver_name, map<string, option_t *> options) {
    squeeze_dimensions();
    cached_solver = solver_name;
    ocp_qp_solver_plan plan = string_to_plan(solver_name);
    std::unique_ptr<void, decltype(&std::free)> args(ocp_qp_create_args(&plan, qp->dim), std::free);

    map<string, option_t *> solver_options;
    auto nested_options = std::make_unique<option<map<string, option_t *>>>(options);
    solver_options[solver_name] = nested_options.get();

    auto flattened_options = map<string, option_t *>();
    flatten(solver_options, flattened_options);

    for (auto opt : flattened_options) {
        string option_name = opt.first;
        option_t *opt_p = opt.second;
        bool found = set_option_int(args.get(), option_name.c_str(), std::to_int(opt_p));
        found |= set_option_double(args.get(), option_name.c_str(), std::to_double(opt_p));
        if (!found)
            throw std::invalid_argument("Option " + option_name + " not known.");
    }
    solver.reset(ocp_qp_create(&plan, qp->dim, args.get()));
    needs_initializing = false;
}

vector<uint> ocp_qp::squeezed_bounds_indices(const string bound, uint stage) {
    vector<uint> bound_indices;
    vector<double> lb = extract("lb" + bound).at(stage), ub = extract("ub" + bound).at(stage);
    auto v = dimensions()["nb" + bound].at(stage);
    for (uint idx = 0; idx < v; ++idx)
        if (lb.at(idx) != -INFINITY || ub.at(idx) != +INFINITY)
            bound_indices.push_back(idx); // there is a double-sided bound at this index
    return bound_indices;
}

void ocp_qp::squeeze_dimensions() {
    map<string, vector<vector<uint>>> idxb;
    map<string, vector<uint>> nb;
    map<string, vector<vector<double>>> lb;
    map<string, vector<vector<double>>> ub;
    for (string bound : {"x", "u"}) {
        for (uint stage = 0; stage <= N; ++stage) {
            idxb[bound].push_back(squeezed_bounds_indices(bound, stage));
            nb[bound].push_back(idxb.at(bound).at(stage).size());
            auto lb_stage = extract("lb" + bound).at(stage);
            auto ub_stage = extract("ub" + bound).at(stage);
            vector<double> lb_new, ub_new;
            for (uint idx = 0; idx < idxb.at(bound).at(stage).size(); ++idx) {
                lb_new.push_back(std::isfinite(lb_stage.at(idx)) ? lb_stage.at(idx) : ACADOS_NEG_INFTY);
                ub_new.push_back(std::isfinite(ub_stage.at(idx)) ? ub_stage.at(idx) : ACADOS_POS_INFTY);
            }
            lb[bound].push_back(lb_new);
            ub[bound].push_back(ub_new);
        }
    }

    d_change_bounds_dimensions_ocp_qp(reinterpret_cast<int *>(nb.at("u").data()),
                                      reinterpret_cast<int *>(nb.at("x").data()),
                                      qp.get());

    for (string bound : {"x", "u"}) {
        for (uint stage = 0; stage <= N; ++stage) {
            set("lb" + bound, stage, lb.at(bound).at(stage));
            set("ub" + bound, stage, ub.at(bound).at(stage));
            set_bounds_indices(bound, stage, idxb.at(bound).at(stage));
        }
    }
}

void ocp_qp::expand_dimensions() {

    map<string, vector<vector<double>>> lb;
    map<string, vector<vector<double>>> ub;

    for (string bound : {"x", "u"}) {
        for (uint stage = 0; stage <= N; ++stage) {
            vector<double> lb_old = extract("lb" + bound).at(stage), ub_old = extract("ub" + bound).at(stage);
            vector<uint> idxb = bounds_indices(bound).at(stage);
            vector<double> lb_new, ub_new;
            for (uint idx = 0, bound_index = 0; idx < dimensions()["n" + bound].at(stage); ++idx) {
                if (bound_index < dimensions()["nb" + bound].at(stage) && idx == idxb.at(bound_index)) {
                    lb_new.push_back(lb_old.at(bound_index));
                    ub_new.push_back(ub_old.at(bound_index));
                    ++bound_index;
                } else {
                    lb_new.push_back(-INFINITY);
                    ub_new.push_back(+INFINITY);
                }
            }
            lb[bound].push_back(lb_new);
            ub[bound].push_back(ub_new);
        }
    }

    d_change_bounds_dimensions_ocp_qp(qp->dim->nu, qp->dim->nx, qp.get());

    for (uint i = 0; i < N; ++i) {
        std::vector<uint> idx_states(dimensions()["nx"].at(i));
        std::iota(std::begin(idx_states), std::end(idx_states), 0);
        set_bounds_indices("x", i, idx_states);

        std::vector<uint> idx_controls(dimensions()["nu"].at(i));
        std::iota(std::begin(idx_controls), std::end(idx_controls), 0);        
        set_bounds_indices("u", i, idx_controls);
    }
    std::vector<uint> idx_states(dimensions()["nx"].at(N));
    std::iota(std::begin(idx_states), std::end(idx_states), 0);
    set_bounds_indices("x", N, idx_states);

    needs_initializing = true;

    for (string bound : {"x", "u"}) {
        for (uint stage = 0; stage <= N; ++stage) {
            set("lb" + bound, stage, lb.at(bound).at(stage));
            set("ub" + bound, stage, ub.at(bound).at(stage));
        }
    }
}

ocp_qp_solution ocp_qp::solve() {

    if (needs_initializing)
        throw std::runtime_error("Reinitialize solver");

    std::cout << *this;

    auto result = std::unique_ptr<ocp_qp_out>(create_ocp_qp_out(qp->dim));

    int_t return_code = ocp_qp_solve(solver.get(), qp.get(), result.get());

    if (return_code != ACADOS_SUCCESS) {
        if (return_code == ACADOS_MAXITER)
            throw std::runtime_error("QP solver " + cached_solver + " reached maximum number of iterations.");
        else if (return_code == ACADOS_MINSTEP)
            throw std::runtime_error("QP solver " + cached_solver + " reached minimum step size.");
        else
            throw std::runtime_error("QP solver " + cached_solver + " failed with solver-specific error code " + std::to_string(return_code));
    }
    return ocp_qp_solution(std::move(result));
}



void ocp_qp::flatten(map<string, option_t *>& input, map<string, option_t *>& output) {
    for (auto opt : input) {
        if (opt.second->nested()) {
            for (auto nested_opt : *opt.second) {
                input.erase(opt.first);
                input[opt.first + "." + nested_opt.first] = nested_opt.second;
                flatten(input, output);
            }
        } else {
            output[opt.first] = opt.second;
        }
    }
}

vector<vector<uint>> ocp_qp::bounds_indices(string name) {

    vector<vector<uint>> idxb;
    if (name == "x") {
        for (uint stage = 0; stage <= N; ++stage) {
            idxb.push_back(vector<uint>());
            for(int i = 0; i < qp->dim->nb[stage]; ++i)
                if (qp->idxb[stage][i] >= qp->dim->nu[stage])
                    idxb.at(stage).push_back(qp->idxb[stage][i] - qp->dim->nu[stage]);
        }
    } else if (name == "u") {
        for (uint stage = 0; stage <= N; ++stage) {
            idxb.push_back(vector<uint>());
            for(int i = 0; i < qp->dim->nb[stage]; ++i)
                if (qp->idxb[stage][i] < qp->dim->nu[stage])
                    idxb.at(stage).push_back(qp->idxb[stage][i]);
        }
    } else throw std::invalid_argument("Can only get bounds from x and u, you gave: '" + name + "'.");
    return idxb;
}

void ocp_qp::set_bounds_indices(string name, uint stage, vector<uint> v) {
    uint nb_bounds;
    if (name == "x")
        nb_bounds = qp->dim->nbx[stage];
    else if (name == "u")
        nb_bounds = qp->dim->nbu[stage];
    else
        throw std::invalid_argument("Can only set bounds on x and u, you gave: '" + name + "'.");

    if (nb_bounds != v.size())
        throw std::invalid_argument("I need " + std::to_string(nb_bounds) + " indices, you gave " + std::to_string(v.size()) + ".");
    for (uint i = 0; i < nb_bounds; ++i)
        if (name == "x")
            qp->idxb[stage][qp->dim->nbu[stage]+i] = qp->dim->nu[stage]+v.at(i);
        else if (name == "u")
            qp->idxb[stage][i] = v.at(i);
}

map<string, std::function<void(int, ocp_qp_in *, double *)>> ocp_qp::extract_functions = {
        {"Q", d_cvt_ocp_qp_to_colmaj_Q},
        {"S", d_cvt_ocp_qp_to_colmaj_S},
        {"R", d_cvt_ocp_qp_to_colmaj_R},
        {"q", d_cvt_ocp_qp_to_colmaj_q},
        {"r", d_cvt_ocp_qp_to_colmaj_r},
        {"A", d_cvt_ocp_qp_to_colmaj_A},
        {"B", d_cvt_ocp_qp_to_colmaj_B},
        {"b", d_cvt_ocp_qp_to_colmaj_b},
        {"lbx", d_cvt_ocp_qp_to_colmaj_lbx},
        {"ubx", d_cvt_ocp_qp_to_colmaj_ubx},
        {"lbu", d_cvt_ocp_qp_to_colmaj_lbu},
        {"ubu", d_cvt_ocp_qp_to_colmaj_ubu},
        {"C", d_cvt_ocp_qp_to_colmaj_C},
        {"D", d_cvt_ocp_qp_to_colmaj_D},
        {"lg", d_cvt_ocp_qp_to_colmaj_lg},
        {"ug", d_cvt_ocp_qp_to_colmaj_ug}
    };


vector< vector<double> > ocp_qp::extract(std::string field) {
    uint last_index = N;
    if (field == "A" || field == "B" || field == "b")
        last_index = N-1;
    vector< vector<double> > result;
    for (uint i = 0; i <= last_index; i++) {
        auto dims = dimensions(field, i);
        vector<double> v(dims.first * dims.second);
        extract_functions[field](i, qp.get(), v.data());
        result.push_back(v);
    }
    return result;
}


map<string, vector<uint>> ocp_qp::dimensions() {
    return {{"nx", nx()}, {"nu", nu()}, {"nbx", nbx()}, {"nbu", nbu()}, {"ng", ng()}};
}


std::vector<uint> ocp_qp::nx() {
    std::vector<uint> tmp(N+1);
    std::copy_n(qp->dim->nx, N+1, tmp.begin());
    return tmp;
}

std::vector<uint> ocp_qp::nu() {
    std::vector<uint> tmp(N+1);
    std::copy_n(qp->dim->nu, N+1, tmp.begin());
    return tmp;
}

std::vector<uint> ocp_qp::nbx() {
    std::vector<uint> tmp(N+1);
    std::copy_n(qp->dim->nbx, N+1, tmp.begin());
    return tmp;
}

std::vector<uint> ocp_qp::nbu() {
    std::vector<uint> tmp(N+1);
    std::copy_n(qp->dim->nbu, N+1, tmp.begin());
    return tmp;
}

std::vector<uint> ocp_qp::ng() {
    std::vector<uint> tmp(N+1);
    std::copy_n(qp->dim->ng, N+1, tmp.begin());
    return tmp;
}

std::ostream& operator<<(std::ostream& oss, const ocp_qp& qp) {
    static char a[1000000];
    print_ocp_qp_in_to_string(a, qp.qp.get());
    oss << a;
    return oss;
}

void ocp_qp::check_range(std::string field, uint stage) {
    uint lower_bound = 0;
    uint upper_bound;
    if (field == "A" || field == "B" || field == "b") {
        upper_bound = N-1;
    } else {
        upper_bound = N;
    }
    if(stage < lower_bound || stage > upper_bound)
        throw std::out_of_range(std::to_string(stage) + " must be in range [" +
              std::to_string(lower_bound) + ", " + std::to_string(upper_bound) + "].");
}

std::pair<uint, uint> ocp_qp::dimensions(std::string field, uint stage) {

    check_range(field, stage);

    if (field == "Q")
        return std::make_pair(num_rows_Q(stage, qp->dim), num_cols_Q(stage, qp->dim));
    else if (field == "S")
        return std::make_pair(num_rows_S(stage, qp->dim), num_cols_S(stage, qp->dim));
    else if (field == "R")
        return std::make_pair(num_rows_R(stage, qp->dim), num_cols_R(stage, qp->dim));
    else if (field == "q")
        return std::make_pair(num_elems_q(stage, qp->dim), 1);
    else if (field == "r")
        return std::make_pair(num_elems_r(stage, qp->dim), 1);
    else if (field == "A")
        return std::make_pair(num_rows_A(stage, qp->dim), num_cols_A(stage, qp->dim));
    else if (field == "B")
        return std::make_pair(num_rows_B(stage, qp->dim), num_cols_B(stage, qp->dim));
    else if (field == "b")
        return std::make_pair(num_elems_b(stage, qp->dim), 1);
    else if (field == "lbx")
        return std::make_pair(num_elems_lbx(stage, qp->dim), 1);
    else if (field == "ubx")
        return std::make_pair(num_elems_ubx(stage, qp->dim), 1);
    else if (field == "lbu")
        return std::make_pair(num_elems_lbu(stage, qp->dim), 1);
    else if (field == "ubu")
        return std::make_pair(num_elems_ubu(stage, qp->dim), 1);
    else if (field == "C")
        return std::make_pair(num_rows_C(stage, qp->dim), num_cols_C(stage, qp->dim));
    else if (field == "D")
        return std::make_pair(num_rows_D(stage, qp->dim), num_cols_D(stage, qp->dim));
    else if (field == "lg")
        return std::make_pair(num_elems_lg(stage, qp->dim), 1);
    else if (field == "ug")
        return std::make_pair(num_elems_ug(stage, qp->dim), 1);
    else
        throw std::invalid_argument("OCP QP does not contain field " + field);
}

static bool match(std::pair<uint, uint> dims, uint nb_elems) {
    uint nb_expected_elems = dims.first * dims.second;
    if (nb_expected_elems == 0 || nb_expected_elems == nb_elems)
        return true;
    return false;
}

void ocp_qp::check_nb_elements(std::string field, uint stage, uint nb_elems) {
    if (!match(dimensions(field, stage), nb_elems))
        throw std::invalid_argument("I need " + std::to_string(dimensions(field, stage)) + " elements but got " + std::to_string(nb_elems) + ".");
}

ocp_qp_solver_plan string_to_plan(string solver) {

    ocp_qp_solver_plan plan;

    if (solver == "condensing_hpipm") {
        plan.qp_solver = FULL_CONDENSING_HPIPM;
    } else if (solver == "sparse_hpipm") {
        plan.qp_solver = PARTIAL_CONDENSING_HPIPM;
    } else if (solver == "hpmpc") {
        plan.qp_solver = PARTIAL_CONDENSING_HPMPC;
    } else if (solver == "ooqp") {
        plan.qp_solver = PARTIAL_CONDENSING_OOQP;
    } else if (solver == "qpdunes") {
        plan.qp_solver = PARTIAL_CONDENSING_QPDUNES;
    } else if (solver == "qpoases") {
        plan.qp_solver = FULL_CONDENSING_QPOASES;
    } else if (solver == "qore") {
        plan.qp_solver = FULL_CONDENSING_QORE;
    } else {
        throw std::invalid_argument("Solver not known.");
    }
    return plan;
}

}  // namespace acados
