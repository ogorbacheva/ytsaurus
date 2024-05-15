RESOURCES_LIBRARY()

# Add new JDK to build/plugins/java.py (2 times)
IF(USE_SYSTEM_JDK)
    MESSAGE(WARNING DEFAULT_JDK are disabled)
ELSEIF(JDK_REAL_VERSION == "22")
    DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON(JDK_DEFAULT jdk22/jdk.json)
    SET_RESOURCE_URI_FROM_JSON(WITH_JDK_URI jdk22/jdk.json)
ELSEIF(JDK_REAL_VERSION == "21")
    DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON(JDK_DEFAULT jdk21/jdk.json)
    SET_RESOURCE_URI_FROM_JSON(WITH_JDK_URI jdk21/jdk.json)
ELSEIF(JDK_REAL_VERSION == "20")
    DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON(JDK_DEFAULT jdk20/jdk.json)
    SET_RESOURCE_URI_FROM_JSON(WITH_JDK_URI jdk20/jdk.json)
ELSEIF(JDK_REAL_VERSION == "17")
    DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON(JDK_DEFAULT jdk17/jdk.json)
    SET_RESOURCE_URI_FROM_JSON(WITH_JDK_URI jdk17/jdk.json)
ELSEIF(JDK_REAL_VERSION == "15")
    DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON(JDK_DEFAULT jdk15/jdk.json)
    SET_RESOURCE_URI_FROM_JSON(WITH_JDK_URI jdk15/jdk.json)
ELSEIF(JDK_REAL_VERSION == "11")
    DECLARE_EXTERNAL_HOST_RESOURCES_BUNDLE_BY_JSON(JDK_DEFAULT jdk11/jdk.json)
    SET_RESOURCE_URI_FROM_JSON(WITH_JDK_URI jdk11/jdk.json)
ELSE()
    MESSAGE(FATAL_ERROR Unsupported JDK version ${JDK_REAL_VERSION})
ENDIF()

IF (WITH_JDK_URI)
    DECLARE_EXTERNAL_RESOURCE(WITH_JDK ${WITH_JDK_URI})
ENDIF()

END()

RECURSE(
    jdk11
    jdk15
    jdk17
    jdk20
    jdk21
    jdk22
    testing
)

IF(YA_IDE_IDEA)
    RECURSE(base_jdk_test)
ENDIF()
