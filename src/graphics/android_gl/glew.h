// glew.h (Android)
//
// Substituto do GLEW para o build Android. O codigo do jogo inclui "glew.h" em
// varios lugares; no desktop isso vem do GLEW (carregador de extensoes do
// OpenGL). No Android as funcoes GLES vem direto do sistema (libGLESv3), entao
// este header inclui os headers GLES 3.2 do NDK e implementa, em cima do GLES,
// as poucas funcoes do OpenGL de desktop que o jogo usa e o GLES nao tem.
//
// Pendencias da Fase 3 (porte de verdade para GLES) estao marcadas com "Fase 3".

#pragma once

#include <GLES3/gl32.h>
#include <GLES2/gl2ext.h>

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

// --- constantes que so existem no OpenGL de desktop ---------------------------

#ifndef GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER
#define GL_FRAMEBUFFER_INCOMPLETE_DRAW_BUFFER 0x8CDB
#endif
#ifndef GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER
#define GL_FRAMEBUFFER_INCOMPLETE_READ_BUFFER 0x8CDC
#endif
// GL_EXT_disjoint_timer_query (so usado pelo contador de FPS). Sem a extensao o
// glBeginQuery gera GL_INVALID_ENUM e o contador fica sem o tempo de GPU.
#ifndef GL_TIME_ELAPSED
#define GL_TIME_ELAPSED 0x88BF
#endif
#ifndef GL_FIRST_VERTEX_CONVENTION
#define GL_FIRST_VERTEX_CONVENTION 0x8E4D
#endif
// No GLES 3 o primitive restart com indice fixo (0xFFFF para GL_UNSIGNED_SHORT,
// que e o que o mapa usa) esta SEMPRE ligado, entao o glEnable/glDisable disso
// nao e necessario. Fase 3: tirar essas chamadas (hoje geram GL_INVALID_ENUM).
#ifndef GL_PRIMITIVE_RESTART
#define GL_PRIMITIVE_RESTART 0x8F9D
#endif
// GLES nao tem esses "caps": MSAA depende so do framebuffer ser multisample, e
// nao existe suavizacao de linha. Fase 3: tirar o glEnable/glDisable deles.
#ifndef GL_MULTISAMPLE
#define GL_MULTISAMPLE 0x809D
#endif
#ifndef GL_LINE_SMOOTH
#define GL_LINE_SMOOTH 0x0B20
#endif

// --- funcoes do OpenGL de desktop implementadas com GLES -----------------------

inline void glClearDepth(GLdouble depth) {
	glClearDepthf(GLfloat(depth));
}

inline void glDepthRange(GLdouble n, GLdouble f) {
	glDepthRangef(GLfloat(n), GLfloat(f));
}

inline void glPrimitiveRestartIndex(GLuint) {
	// ver GL_PRIMITIVE_RESTART acima -- o indice no GLES e sempre o maximo do tipo
}

inline void glProvokingVertex(GLenum) {
	// Fase 3: GLES usa sempre o ULTIMO vertice como provocante (para varyings
	// "flat"); o jogo pede o primeiro. Shaders com "flat" podem precisar de ajuste.
}

// O jogo so usa glBufferStorage com flags == 0 (buffer imutavel, sem map),
// que e equivalente a um glBufferData estatico.
inline void glBufferStorage(GLenum target, GLsizeiptr size, const void* data, GLbitfield) {
	glBufferData(target, size, data, GL_STATIC_DRAW);
}

inline void glMultiDrawArrays(GLenum mode, const GLint* first, const GLsizei* count, GLsizei drawcount) {
	for(GLsizei i = 0; i < drawcount; ++i) {
		if(count[i] > 0)
			glDrawArrays(mode, first[i], count[i]);
	}
}

// indirect = deslocamento dentro do GL_DRAW_INDIRECT_BUFFER ligado (GLES 3.1+)
inline void glMultiDrawArraysIndirect(GLenum mode, const void* indirect, GLsizei drawcount, GLsizei stride) {
	GLsizei const step = stride != 0 ? stride : GLsizei(4 * sizeof(GLuint));
	for(GLsizei i = 0; i < drawcount; ++i) {
		glDrawArraysIndirect(mode, static_cast<const char*>(indirect) + intptr_t(i) * step);
	}
}

// GLES nao aceita formato nao-dimensionado (GL_RGBA) em texturas multisample
inline void glTexImage2DMultisample(GLenum target, GLsizei samples, GLenum internalformat, GLsizei width, GLsizei height, GLboolean fixedsamplelocations) {
	if(internalformat == GL_RGBA)
		internalformat = GL_RGBA8;
	else if(internalformat == GL_RGB)
		internalformat = GL_RGB8;
	glTexStorage2DMultisample(target, samples, internalformat, width, height, fixedsamplelocations);
}

namespace android_gl_detail {
inline GLsizei bytes_per_pixel(GLenum format, GLenum type) {
	GLsizei channels = 4;
	switch(format) {
	case GL_RED: case GL_RED_INTEGER: channels = 1; break;
	case GL_RG: case GL_RG_INTEGER: channels = 2; break;
	case GL_RGB: case GL_RGB_INTEGER: channels = 3; break;
	default: channels = 4; break;
	}
	switch(type) {
	case GL_FLOAT: case GL_INT: case GL_UNSIGNED_INT: return channels * 4;
	case GL_HALF_FLOAT: case GL_SHORT: case GL_UNSIGNED_SHORT: return channels * 2;
	default: return channels;
	}
}

// liga a textura (GL_TEXTURE_2D) num framebuffer temporario, para ler/limpar
struct scoped_texture_fbo {
	GLint previous_read = 0;
	GLint previous_draw = 0;
	GLuint fbo = 0;
	scoped_texture_fbo(GLuint texture, GLint level) {
		glGetIntegerv(GL_READ_FRAMEBUFFER_BINDING, &previous_read);
		glGetIntegerv(GL_DRAW_FRAMEBUFFER_BINDING, &previous_draw);
		glGenFramebuffers(1, &fbo);
		glBindFramebuffer(GL_FRAMEBUFFER, fbo);
		glFramebufferTexture2D(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, level);
	}
	~scoped_texture_fbo() {
		glBindFramebuffer(GL_READ_FRAMEBUFFER, GLuint(previous_read));
		glBindFramebuffer(GL_DRAW_FRAMEBUFFER, GLuint(previous_draw));
		glDeleteFramebuffers(1, &fbo);
	}
};

inline void texture_size(GLuint texture, GLint level, GLint& w, GLint& h) {
	GLint previous = 0;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
	glBindTexture(GL_TEXTURE_2D, texture);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_WIDTH, &w);
	glGetTexLevelParameteriv(GL_TEXTURE_2D, level, GL_TEXTURE_HEIGHT, &h);
	glBindTexture(GL_TEXTURE_2D, GLuint(previous));
}
} // namespace android_gl_detail

// So para texturas GL_TEXTURE_2D (o unico uso no jogo).
inline void glClearTexImage(GLuint texture, GLint level, GLenum format, GLenum type, const void* data) {
	GLint w = 0, h = 0;
	android_gl_detail::texture_size(texture, level, w, h);
	if(w <= 0 || h <= 0)
		return;
	GLsizei const bpp = android_gl_detail::bytes_per_pixel(format, type);
	uint8_t* pixels = static_cast<uint8_t*>(calloc(size_t(w) * size_t(h), size_t(bpp)));
	if(!pixels)
		return;
	if(data) {
		for(size_t i = 0; i < size_t(w) * size_t(h); ++i)
			memcpy(pixels + i * bpp, data, size_t(bpp));
	}
	GLint previous = 0, previous_alignment = 4;
	glGetIntegerv(GL_TEXTURE_BINDING_2D, &previous);
	glGetIntegerv(GL_UNPACK_ALIGNMENT, &previous_alignment);
	glBindTexture(GL_TEXTURE_2D, texture);
	glPixelStorei(GL_UNPACK_ALIGNMENT, 1);
	glTexSubImage2D(GL_TEXTURE_2D, level, 0, 0, w, h, format, type, pixels);
	glPixelStorei(GL_UNPACK_ALIGNMENT, previous_alignment);
	glBindTexture(GL_TEXTURE_2D, GLuint(previous));
	free(pixels);
}

// GLES nao le texturas direto; le pelo framebuffer com glReadPixels.
// So para texturas GL_TEXTURE_2D com formato legivel como cor (RGBA8 etc.).
inline void glGetTextureImage(GLuint texture, GLint level, GLenum format, GLenum type, GLsizei bufSize, void* pixels) {
	GLint w = 0, h = 0;
	android_gl_detail::texture_size(texture, level, w, h);
	if(w <= 0 || h <= 0 || GLsizei(w * h * android_gl_detail::bytes_per_pixel(format, type)) > bufSize)
		return;
	GLint previous_alignment = 4;
	glGetIntegerv(GL_PACK_ALIGNMENT, &previous_alignment);
	glPixelStorei(GL_PACK_ALIGNMENT, 1);
	{
		android_gl_detail::scoped_texture_fbo fbo{ texture, level };
		glReadPixels(0, 0, w, h, format, type, pixels);
	}
	glPixelStorei(GL_PACK_ALIGNMENT, previous_alignment);
}

inline void glGetQueryObjectiv(GLuint id, GLenum pname, GLint* params) {
	GLuint value = 0;
	glGetQueryObjectuiv(id, pname, &value);
	*params = GLint(value);
}
