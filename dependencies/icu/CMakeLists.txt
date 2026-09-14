if(ANDROID)
	# ICU4C nativo do NDK (Android 12 / API 31+, NDK r22b+) -- sem find_package,
	# sem compilar/vendorizar ICU. So expoe um subconjunto da API C: falta
	# unicode/ubidi.h inteiro e ubrk_openBinaryRules/ubrk_getBinaryRules --
	# ver src/text/icu_android_compat.hpp pros contornos.
	find_library(ALICE_NDK_ICU_LIB icu)
	if(NOT ALICE_NDK_ICU_LIB)
		message(FATAL_ERROR "libicu do NDK nao encontrada -- confira se ANDROID_PLATFORM >= 31 (API 31, Android 12) no preset.")
	endif()
	add_library(dependency_icu INTERFACE)
	target_link_libraries(dependency_icu INTERFACE ${ALICE_NDK_ICU_LIB})
else()
	message(STATUS "Fetching icu...")

	find_package(ICU 70.1 COMPONENTS uc i18n REQUIRED)

	add_library(dependency_icu INTERFACE)
	target_link_libraries(dependency_icu INTERFACE ICU::uc ICU::i18n)
endif()
