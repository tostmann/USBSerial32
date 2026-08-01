idf_component_register(SRCS "serial_bridge.c" "probe.c"
                       INCLUDE_DIRS ".")

# Produkt kommt von aussen: build.sh uebergibt -DBW_PRODUCT=TUL bzw. EUL.
# Ohne diese Angabe wird bewusst nicht gebaut -- ein Default waere genau die
# Sorte stille Annahme, die am Ende falsche Pins auf echte Hardware bringt.
if(NOT DEFINED BW_PRODUCT)
    message(FATAL_ERROR
        "BW_PRODUCT nicht gesetzt. Mit build.sh bauen, oder "
        "idf.py -DBW_PRODUCT=TUL ... bzw. -DBW_PRODUCT=EUL ...")
endif()

if(NOT DEFINED BW_VERSION)
    set(BW_VERSION "0.0.0")
endif()
if(NOT DEFINED BW_QUIET)
    set(BW_QUIET 0)
endif()

target_compile_definitions(${COMPONENT_LIB} PRIVATE
    BUSWARE_${BW_PRODUCT}=1
    BW_VERSION="${BW_VERSION}"
    BW_QUIET=${BW_QUIET})
