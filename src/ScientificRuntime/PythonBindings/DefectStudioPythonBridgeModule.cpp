#include <cmath>
#include <string>

#include <nanobind/nanobind.h>
#include <nanobind/stl/string.h>

namespace nb = nanobind;

namespace DefectStudio
{
	struct BridgeVector3
	{
		double x = 0.0;
		double y = 0.0;
		double z = 0.0;
	};

	double Distance(const BridgeVector3 &left, const BridgeVector3 &right)
	{
		const double dx = left.x - right.x;
		const double dy = left.y - right.y;
		const double dz = left.z - right.z;
		return std::sqrt(dx * dx + dy * dy + dz * dz);
	}
} // namespace DefectStudio

NB_MODULE(ds_python_bridge, module)
{
	module.doc() = "DefectStudio dummy Python bridge module";

	module.def("add", [](int left, int right) { return left + right; });
	module.def("hello_from_cpp", []() { return std::string("hello from C++/nanobind"); });

	nb::class_<DefectStudio::BridgeVector3>(module, "BridgeVector3")
		.def(nb::init<>())
		.def_rw("x", &DefectStudio::BridgeVector3::x)
		.def_rw("y", &DefectStudio::BridgeVector3::y)
		.def_rw("z", &DefectStudio::BridgeVector3::z);

	module.def("distance", &DefectStudio::Distance);
}
