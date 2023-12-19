#include "idaklu/casadi_solver.hpp"
#include "idaklu/common.hpp"
#include "idaklu/python.hpp"

#include <pybind11/functional.h>
#include <pybind11/numpy.h>
#include <pybind11/pybind11.h>
#include <pybind11/stl.h>
#include <pybind11/stl_bind.h>

#include <vector>
#include <iostream>
#include <functional>

Function generate_function(const std::string &data)
{
  return Function::deserialize(data);
}

namespace py = pybind11;

PYBIND11_MAKE_OPAQUE(std::vector<np_array>);

int global_var = 0;
using Handler = std::function<np_array(np_array, realtype, realtype)>;
using HandlerJvp = std::function<np_array(np_array, realtype, realtype, np_array, realtype, realtype)>;
using HandlerVjp = std::function<np_array(np_array, realtype, np_array, realtype, realtype)>;

Handler handler;
HandlerJvp handler_jvp;
HandlerVjp handler_vjp;

np_array test_capsule() {
  std::cout << "test_capsule" << std::endl;
  int count = 10;
  realtype *t_return = new realtype[count];
  py::capsule free_t_when_done(
    t_return,
    [](void *f) {
      realtype *vect = reinterpret_cast<realtype *>(f);
      delete[] vect;
    }
  );
  for (int n = 0; n < count; ++n) {
    t_return[n] = (realtype) n;
  }
  np_array t_ret = np_array(
    {count},
    {sizeof(realtype)},
    &t_return[0],
    free_t_when_done
  );
  std::cout << "t_ret: done" << std::endl;
  return t_ret;
}

void cpu_idaklu(void *out_tuple, const void **in) {
  // Parse the inputs --- note that these come from jax lowering and are NOT np_array's
  int k = 0;
  const std::int64_t n_t = *reinterpret_cast<const std::int64_t *>(in[k++]);
  const std::int64_t n_vars = *reinterpret_cast<const std::int64_t *>(in[k++]);
  const realtype *t = reinterpret_cast<const realtype *>(in[k++]);
  const realtype *in1 = reinterpret_cast<const realtype *>(in[k++]);
  const realtype *in2 = reinterpret_cast<const realtype *>(in[k++]);
  void *out = reinterpret_cast<realtype *>(out_tuple);
  
  // Log
  std::cout << "n_t: " << n_t << std::endl;
  std::cout << "n_vars: " << n_vars << std::endl;
  for (std::int64_t n = 0; n < n_t; ++n) {
    std::cout << "t: " << t[n] << std::endl;
  }
  std::cout << "in1: " << in1[0] << std::endl;
  std::cout << "in2: " << in2[0] << std::endl;
  
  // Form time vector as an np_array
  py::capsule t_capsule(t, "t_capsule");
  np_array t_np = np_array({n_t}, {sizeof(realtype)}, t, t_capsule);

  // Call solve obtain function in python to obtain an np_array
  np_array out_np = handler(t_np, in1[0], in2[0]);
  auto out_buf = out_np.request();
  const realtype* out_ptr = reinterpret_cast<realtype*>(out_buf.ptr);

  // Arrange into 'out' array
  memcpy(out, out_ptr, n_t*n_vars*sizeof(realtype));
}

void cpu_idaklu_jvp(void *out_tuple, const void **in) {
  // Parse the inputs --- note that these come from jax lowering and are NOT np_array's
  int k = 0;
  const std::int64_t n_t = *reinterpret_cast<const std::int64_t *>(in[k++]);
  const std::int64_t n_vars = *reinterpret_cast<const std::int64_t *>(in[k++]);
  const realtype *primal_t = reinterpret_cast<const realtype *>(in[k++]);
  const realtype *primal_in1 = reinterpret_cast<const realtype *>(in[k++]);
  const realtype *primal_in2 = reinterpret_cast<const realtype *>(in[k++]);
  const realtype *tangent_t = reinterpret_cast<const realtype *>(in[k++]);
  const realtype *tangent_in1 = reinterpret_cast<const realtype *>(in[k++]);
  const realtype *tangent_in2 = reinterpret_cast<const realtype *>(in[k++]);
  void *out = reinterpret_cast<realtype *>(out_tuple);

  // Log
  std::cout << "cpu_idaklu_jvp" << std::endl;
  std::cout << "n_t: " << n_t << std::endl;
  std::cout << "n_vars: " << n_vars << std::endl;
  for (std::int64_t n = 0; n < n_t; ++n) {
    std::cout << "primal_t: " << primal_t[n] << std::endl;
    std::cout << "tangent_t: " << tangent_t[n] << std::endl;
  }
  std::cout << "primal_in1: " << primal_in1[0] << std::endl;
  std::cout << "primal_in2: " << primal_in2[0] << std::endl;
  std::cout << "tangent_in1: " << tangent_in1[0] << std::endl;
  std::cout << "tangent_in2: " << tangent_in2[0] << std::endl;
  
  // Form time vector as an np_array
  py::capsule primal_t_capsule(primal_t, "primal_t_capsule");
  np_array primal_t_np = np_array(
    {n_t},
    {sizeof(realtype)},
    primal_t,
    primal_t_capsule
  );
  py::capsule tangent_t_capsule(tangent_t, "tangent_t_capsule");
  np_array tangent_t_np = np_array(
    {n_t},
    {sizeof(realtype)},
    tangent_t,
    tangent_t_capsule
  );

  // Call JVP function in python to obtain an np_array
  np_array y_dot = handler_jvp(
    primal_t_np, primal_in1[0], primal_in2[0],
    tangent_t_np, tangent_in1[0], tangent_in2[0]
  );
  auto buf = y_dot.request();
  const realtype* ptr = reinterpret_cast<realtype*>(buf.ptr);

  // Arrange into 'out' array
  memcpy(out, ptr, n_t*n_vars*sizeof(realtype));
}

void cpu_idaklu_vjp(void *out_tuple, const void **in) {
  int k = 0;
  const std::int64_t n_t = *reinterpret_cast<const std::int64_t *>(in[k++]);
  const std::int64_t n_vars = *reinterpret_cast<const std::int64_t *>(in[k++]);
  const realtype *y_bar = reinterpret_cast<const realtype *>(in[k++]);
  const std::int64_t *invar = reinterpret_cast<const std::int64_t *>(in[k++]);
  const realtype *t = reinterpret_cast<const realtype *>(in[k++]);
  const realtype *in1 = reinterpret_cast<const realtype *>(in[k++]);
  const realtype *in2 = reinterpret_cast<const realtype *>(in[k++]);
  void *out = reinterpret_cast<realtype *>(out_tuple);

  // Log
  std::cout << "cpu_idaklu_vjp" << std::endl;
  std::cout << "n_t: " << n_t << std::endl;
  std::cout << "n_vars: " << n_vars << std::endl;
  std::cout << "invar: " << invar[0] << std::endl;
  std::cout << "y_bar:" << std::endl;
  for (std::int64_t n = 0; n < n_vars; ++n) {
    std::cout << "  [" << n << "] = " << y_bar[n] << std::endl;
  }
  for (std::int64_t n = 0; n < n_t; ++n) {
    std::cout << "t: " << t[n] << std::endl;
  }
  std::cout << "in1: " << in1[0] << std::endl;
  std::cout << "in2: " << in2[0] << std::endl;
  
  // Form time vector as an np_array
  py::capsule t_capsule(t, "t_capsule");
  np_array t_np = np_array({n_t}, {sizeof(realtype)}, t, t_capsule);
  
  // Form y_bar as an np_array
  py::capsule y_bar_capsule(t, "y_bar_capsule");
  np_array y_bar_np = np_array({n_vars}, {sizeof(realtype)}, y_bar, y_bar_capsule);

  // Call VJP function in python to obtain an np_array
  np_array y_dot = handler_vjp(y_bar_np, invar[0], t_np, in1[0], in2[0]);
  auto buf = y_dot.request();
  const realtype* ptr = reinterpret_cast<realtype*>(buf.ptr);

  // Arrange output --- TODO
  memcpy(out, ptr, n_t*sizeof(realtype));
}

template <typename T>
pybind11::capsule EncapsulateFunction(T* fn) {
  return pybind11::capsule((void*)(fn), "xla._CUSTOM_CALL_TARGET");
}

pybind11::dict Registrations() {
  pybind11::dict dict;
  dict["cpu_idaklu_f64"] = EncapsulateFunction(cpu_idaklu);
  dict["cpu_idaklu_jvp_f64"] = EncapsulateFunction(cpu_idaklu_jvp);
  dict["cpu_idaklu_vjp_f64"] = EncapsulateFunction(cpu_idaklu_vjp);
  return dict;
}

PYBIND11_MODULE(idaklu, m)
{
  m.doc() = "sundials solvers"; // optional module docstring

  py::bind_vector<std::vector<np_array>>(m, "VectorNdArray");

  m.def("solve_python", &solve_python,
    "The solve function for python evaluators",
    py::arg("t"),
    py::arg("y0"),
    py::arg("yp0"),
    py::arg("res"),
    py::arg("jac"),
    py::arg("sens"),
    py::arg("get_jac_data"),
    py::arg("get_jac_row_vals"),
    py::arg("get_jac_col_ptr"),
    py::arg("nnz"),
    py::arg("events"),
    py::arg("number_of_events"),
    py::arg("use_jacobian"),
    py::arg("rhs_alg_id"),
    py::arg("atol"),
    py::arg("rtol"),
    py::arg("inputs"),
    py::arg("number_of_sensitivity_parameters"),
    py::return_value_policy::take_ownership);

  py::class_<CasadiSolver>(m, "CasadiSolver")
  .def("solve", &CasadiSolver::solve,
    "perform a solve",
    py::arg("t"),
    py::arg("y0"),
    py::arg("yp0"),
    py::arg("inputs"),
    py::return_value_policy::take_ownership);

  //py::bind_vector<std::vector<Function>>(m, "VectorFunction");
  //py::implicitly_convertible<py::iterable, std::vector<Function>>();

  m.def("create_casadi_solver", &create_casadi_solver,
    "Create a casadi idaklu solver object",
    py::arg("number_of_states"),
    py::arg("number_of_parameters"),
    py::arg("rhs_alg"),
    py::arg("jac_times_cjmass"),
    py::arg("jac_times_cjmass_colptrs"),
    py::arg("jac_times_cjmass_rowvals"),
    py::arg("jac_times_cjmass_nnz"),
    py::arg("jac_bandwidth_lower"),
    py::arg("jac_bandwidth_upper"),
    py::arg("jac_action"),
    py::arg("mass_action"),
    py::arg("sens"),
    py::arg("events"),
    py::arg("number_of_events"),
    py::arg("rhs_alg_id"),
    py::arg("atol"),
    py::arg("rtol"),
    py::arg("inputs"),
    py::arg("var_casadi_fcns"),
    py::arg("dvar_dy_fcns"),
    py::arg("dvar_dp_fcns"),
    py::arg("options"),
    py::return_value_policy::take_ownership);

  m.def("generate_function", &generate_function,
    "Generate a casadi function",
    py::arg("string"),
    py::return_value_policy::take_ownership);
  
  m.def("registrations", &Registrations);
  m.def("register_callback_jaxsolve",
    [](Handler h) { handler = h; });
  m.def("register_callback_jvp",
    [](HandlerJvp h) { handler_jvp = h; });
  m.def("register_callback_vjp",
    [](HandlerVjp h) { handler_vjp = h; });
  m.def("register_callbacks",
    [](Handler h, HandlerJvp h_jvp, HandlerVjp h_vjp) {
      handler = h;
      handler_jvp = h_jvp;
      handler_vjp = h_vjp;
    });
  m.def("test_capsule", &test_capsule);

  py::class_<Function>(m, "Function");

  py::class_<Solution>(m, "solution")
  .def_readwrite("t", &Solution::t)
  .def_readwrite("y", &Solution::y)
  .def_readwrite("yS", &Solution::yS)
  .def_readwrite("flag", &Solution::flag);
}
