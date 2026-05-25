#include "Core/dspch.hpp"

#include "Renderer/OpenGl/OpenGlFrameBuffer.hpp"

#include <algorithm>

#include <glad/gl.h>

#include "Core/Utils/Logger.hpp"

namespace DefectStudio
{
	OpenGlFrameBuffer::~OpenGlFrameBuffer()
	{
		release();
	}

	void OpenGlFrameBuffer::Resize(int width, int height)
	{
		const int safeWidth = std::max(width, 1);
		const int safeHeight = std::max(height, 1);
		if (m_Width == safeWidth && m_Height == safeHeight && m_FrameBuffer != 0)
			return;

		release();
		m_Width = safeWidth;
		m_Height = safeHeight;

		glCreateFramebuffers(1, &m_FrameBuffer);
		glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBuffer);

		glCreateTextures(GL_TEXTURE_2D, 1, &m_ColorTexture);
		glBindTexture(GL_TEXTURE_2D, m_ColorTexture);
		glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA8, m_Width, m_Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, nullptr);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, GL_LINEAR);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, GL_CLAMP_TO_EDGE);
		glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, GL_CLAMP_TO_EDGE);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, m_ColorTexture, 0);

		glCreateRenderbuffers(1, &m_DepthStencil);
		glBindRenderbuffer(GL_RENDERBUFFER, m_DepthStencil);
		glRenderbufferStorage(GL_RENDERBUFFER, GL_DEPTH24_STENCIL8, m_Width, m_Height);
		glFramebufferRenderbuffer(GL_FRAMEBUFFER, GL_DEPTH_STENCIL_ATTACHMENT, GL_RENDERBUFFER, m_DepthStencil);

		if (glCheckFramebufferStatus(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE)
			DS_LOG_ERROR("Renderer framebuffer is incomplete for size {}x{}", m_Width, m_Height);
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	void OpenGlFrameBuffer::Bind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, m_FrameBuffer);
	}

	void OpenGlFrameBuffer::Unbind() const
	{
		glBindFramebuffer(GL_FRAMEBUFFER, 0);
	}

	unsigned int OpenGlFrameBuffer::ColorTextureId() const
	{
		return m_ColorTexture;
	}

	int OpenGlFrameBuffer::Width() const
	{
		return m_Width;
	}

	int OpenGlFrameBuffer::Height() const
	{
		return m_Height;
	}

	void OpenGlFrameBuffer::release()
	{
		if (m_DepthStencil != 0)
		{
			glDeleteRenderbuffers(1, &m_DepthStencil);
			m_DepthStencil = 0;
		}
		if (m_ColorTexture != 0)
		{
			glDeleteTextures(1, &m_ColorTexture);
			m_ColorTexture = 0;
		}
		if (m_FrameBuffer != 0)
		{
			glDeleteFramebuffers(1, &m_FrameBuffer);
			m_FrameBuffer = 0;
		}
	}
} // namespace DefectStudio
