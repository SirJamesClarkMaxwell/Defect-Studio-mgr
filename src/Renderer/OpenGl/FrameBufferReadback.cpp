#include "Core/dspch.hpp"

#include "Renderer/OpenGl/FrameBufferReadback.hpp"

#include <cstring>

#include <glad/gl.h>

namespace DefectStudio
{
	std::vector<unsigned char> ReadRgba8TopDown(int x, int y, int width, int height)
	{
		if (width <= 0 || height <= 0)
			return {};

		const std::size_t rowBytes = static_cast<std::size_t>(width) * 4u;
		std::vector<unsigned char> bottomUp(rowBytes * static_cast<std::size_t>(height));
		glReadPixels(x, y, width, height, GL_RGBA, GL_UNSIGNED_BYTE, bottomUp.data());

		std::vector<unsigned char> topDown(bottomUp.size());
		for (int row = 0; row < height; ++row)
		{
			std::memcpy(
				topDown.data() + static_cast<std::size_t>(row) * rowBytes,
				bottomUp.data() + static_cast<std::size_t>(height - 1 - row) * rowBytes,
				rowBytes);
		}
		return topDown;
	}
} // namespace DefectStudio
