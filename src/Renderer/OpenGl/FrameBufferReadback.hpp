#pragma once

#include <vector>

namespace DefectStudio
{
	// Reads a rectangle of the currently bound read framebuffer back as RGBA8 and returns it top-down
	// (row 0 == the top row of the image). GL hands rows back bottom-up; PNG, image diffing and every
	// human reading a pixel coordinate want the opposite, and doing that flip in two places is how the
	// exporter and a test end up disagreeing about which way is up.
	//
	// `x` / `y` are in GL's own bottom-left-origin space, exactly as glReadPixels takes them - this
	// function flips rows, it does not reinterpret the requested rectangle. The caller owns binding and
	// unbinding the framebuffer. A non-positive width or height returns an empty vector.
	[[nodiscard]] std::vector<unsigned char> ReadRgba8TopDown(int x, int y, int width, int height);
}
