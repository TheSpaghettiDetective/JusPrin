# Fails the build when JusPrin's own code converts a wxString with
# ToStdString(). It converts with wxConvLibc, the encoding of the C library's
# locale, while JusPrin's std::strings are UTF-8. On Windows that encoding is
# the system's code page, such as Windows-1252, and in the plain "C" locale
# ToStdString() returns "" for any text with a letter outside ASCII, such as a
# translated label. Use std::string(text.ToUTF8()) instead.
#
# Run as: cmake -DJUSPRIN_DIR=<this folder> -DSTAMP=<file> -P CheckWxStringConversions.cmake
file(GLOB_RECURSE sources "${JUSPRIN_DIR}/*.cpp" "${JUSPRIN_DIR}/*.hpp" "${JUSPRIN_DIR}/*.h" "${JUSPRIN_DIR}/*.mm")
list(FILTER sources EXCLUDE REGEX "/node_modules/")
set(found "")
foreach (source IN LISTS sources)
    file(STRINGS "${source}" lines REGEX "ToStdString[ \t]*\\(")
    foreach (line IN LISTS lines)
        file(RELATIVE_PATH name "${JUSPRIN_DIR}" "${source}")
        string(STRIP "${line}" line)
        string(APPEND found "\n  ${name}: ${line}")
    endforeach ()
endforeach ()
if (found)
    message(FATAL_ERROR "JusPrin code calls wxString::ToStdString(), which loses non-ASCII text outside a UTF-8 locale. "
                        "Use std::string(text.ToUTF8()) instead (see CheckWxStringConversions.cmake):${found}")
endif ()
file(TOUCH "${STAMP}")
