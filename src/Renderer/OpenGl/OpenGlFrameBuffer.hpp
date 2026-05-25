#pragma once

namespace DefectStudio
{
	class OpenGlFrameBuffer
	{
	public:
		OpenGlFrameBuffer() = default;
		~OpenGlFrameBuffer();

		void Resize(int width, int height);
		void Bind() const;
		void Unbind() const;
		[[nodiscard]] unsigned int ColorTextureId() const;
		[[nodiscard]] int Width() const;
		[[nodiscard]] int Height() const;

	private:
		void release();

	private:
		unsigned int m_FrameBuffer = 0;
		unsigned int m_ColorTexture = 0;
		unsigned int m_DepthStencil = 0;
		int m_Width = 0;
		int m_Height = 0;
	};
} // namespace DefectStudio

