#include "Core/dspch.hpp"

#include "App/Application.hpp"
using namespace DefectStudio;
int main(int argc, char **argv)
{
	Application app = Application::Create(argc,argv);
	app.Run();
}
