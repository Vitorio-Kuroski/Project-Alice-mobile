// icu_android_compat.hpp
//
// A API publica do ICU4C exposta pelo NDK (Android 12 / API 31+, NDK r22b+)
// e so um SUBCONJUNTO do ICU4C completo. Dois buracos relevantes pro Alice:
//
//  1) <unicode/ubidi.h> inteiro nao existe no NDK -- bidi (texto arabe/hebraico
//     da direita pra esquerda) nao tem equivalente nenhum la.
//     TODO (pos-Fase 1): dar suporte a RTL de verdade no Android, seja
//     vendorizando um algoritmo de bidi (ex. um port leve do algoritmo Unicode
//     BiDi, tipo fribidi) ou outra fonte de ICU completo. Por enquanto o stub
//     abaixo sempre trata o texto como uma unica run LTR -- jogos com locale
//     RTL (arabe, hebraico) vao ficar com o texto na ordem visual errada no
//     Android ate isso ser resolvido.
//
//  2) ubrk_openBinaryRules()/ubrk_getBinaryRules() nao existem no NDK (o Alice
//     usa isso pra pre-compilar as regras de quebra de linha/palavra/char uma
//     vez e reusar). O ubrk_open() comum (que abre a partir de um locale) esse
//     sim existe -- entao aqui a gente reaproveita os MESMOS vetores
//     compiled_ubrk_rules/compiled_char_ubrk_rules/compiled_word_ubrk_rules
//     (ver fonts.hpp) só que, no Android, em vez de bytes de regra compilada,
//     eles guardam [1 byte de UBreakIteratorType][bytes do locale em UTF-8].
//     ubrk_openBinaryRules() abaixo decodifica isso e chama ubrk_open() de verdade.
#pragma once

#ifdef __ANDROID__

#include <cstring>
#include <cstdint>

// ---- bidi: sempre uma unica run LTR (RTL desligado por enquanto) ----

using UBiDiLevel = uint8_t;
enum UBiDiDirection { UBIDI_LTR = 0, UBIDI_RTL = 1, UBIDI_MIXED = 2, UBIDI_NEUTRAL = 3 };

struct UBiDi {
	int32_t text_length = 0;
};

inline UBiDi* ubidi_open() {
	return new UBiDi();
}

inline UBiDi* ubidi_openSized(int32_t /*maxLength*/, int32_t /*maxRunCount*/, UErrorCode* pErrorCode) {
	*pErrorCode = U_ZERO_ERROR;
	return new UBiDi();
}

inline void ubidi_close(UBiDi* bidi) {
	delete bidi;
}

inline void ubidi_setPara(UBiDi* bidi, const UChar* /*text*/, int32_t length, UBiDiLevel /*paraLevel*/,
		UBiDiLevel* /*embeddingLevels*/, UErrorCode* pErrorCode) {
	// TODO (pos-Fase 1): respeitar paraLevel/locale_get_native_rtl uma vez que
	// exista suporte a bidi no Android -- por enquanto sempre LTR.
	bidi->text_length = length;
	*pErrorCode = U_ZERO_ERROR;
}

inline int32_t ubidi_countRuns(UBiDi* /*bidi*/, UErrorCode* pErrorCode) {
	*pErrorCode = U_ZERO_ERROR;
	return 1; // uma unica run, sempre
}

inline UBiDiDirection ubidi_getVisualRun(UBiDi* bidi, int32_t /*runIndex*/, int32_t* logicalStart, int32_t* length) {
	*logicalStart = 0;
	*length = bidi->text_length;
	return UBIDI_LTR;
}

// ---- ubrk_openBinaryRules: decodifica [tipo][locale] e chama ubrk_open() ----

inline UBreakIterator* ubrk_openBinaryRules(const uint8_t* binaryRules, int32_t rulesLength,
		const UChar* text, int32_t textLength, UErrorCode* status) {
	if(rulesLength < 1) {
		*status = U_ILLEGAL_ARGUMENT_ERROR;
		return nullptr;
	}
	auto type = UBreakIteratorType(binaryRules[0]);
	// bytes seguintes = locale em UTF-8/ASCII (sempre é, ver fonts.cpp) -- precisa
	// de um char* terminado em zero pro ubrk_open, entao copia pra um buffer local
	char locale_buf[64] = {};
	int32_t locale_len = rulesLength - 1;
	if(locale_len >= int32_t(sizeof(locale_buf)))
		locale_len = int32_t(sizeof(locale_buf)) - 1;
	std::memcpy(locale_buf, binaryRules + 1, size_t(locale_len));
	locale_buf[locale_len] = '\0';
	return ubrk_open(type, locale_buf, text, textLength, status);
}

#endif // __ANDROID__
