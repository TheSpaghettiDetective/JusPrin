# Translation catalogs that carry the product name. Built from upstream's .po
# files by brand_catalogs.py into resources/i18n/<lang>/${SLIC3R_APP_KEY}.mo,
# the file the application loads, so no upstream string or macro is edited.
find_program(JUSPRIN_PYTHON NAMES python3 python)
if (NOT JUSPRIN_PYTHON)
    message(WARNING "python3 not found: JusPrin translation catalogs will not be built; the UI keeps upstream product names")
else()
    set(JUSPRIN_CATALOG_SCRIPT "${CMAKE_CURRENT_LIST_DIR}/brand_catalogs.py")
    set(JUSPRIN_CATALOG_STAMP  "${CMAKE_BINARY_DIR}/jusprin_catalogs.stamp")
    file(GLOB JUSPRIN_PO_FILES "${CMAKE_SOURCE_DIR}/localization/i18n/*/OrcaSlicer_*.po")
    add_custom_command(
        OUTPUT  "${JUSPRIN_CATALOG_STAMP}"
        COMMAND "${JUSPRIN_PYTHON}" "${JUSPRIN_CATALOG_SCRIPT}"
                --pot "${CMAKE_SOURCE_DIR}/localization/i18n/OrcaSlicer.pot"
                --po-dir "${CMAKE_SOURCE_DIR}/localization/i18n"
                --out-dir "${SLIC3R_RESOURCES_DIR}/i18n"
                --domain "${SLIC3R_APP_KEY}"
                --product "${SLIC3R_APP_NAME}"
                --stamp "${JUSPRIN_CATALOG_STAMP}"
        DEPENDS "${JUSPRIN_CATALOG_SCRIPT}" "${CMAKE_SOURCE_DIR}/localization/i18n/OrcaSlicer.pot" ${JUSPRIN_PO_FILES}
        COMMENT "Building JusPrin translation catalogs"
        VERBATIM)
    add_custom_target(jusprin_catalogs ALL DEPENDS "${JUSPRIN_CATALOG_STAMP}")
endif()
