#===============================================================================
# @brief    cmake file
# Copyright (c) CompanyNameMagicTag 2023-2023. All rights reserved.
#===============================================================================
if(DEFINED CONFIG_SAMPLE_SUPPORT_BINARY_SEMAPHORE)
    add_subdirectory_if_exist(binary_semaphore)
endif()

if(DEFINED CONFIG_SAMPLE_SUPPORT_COUNT_SEMAPHORE)
    add_subdirectory_if_exist(count_semaphore)
endif()

set(SOURCES "${SOURCES}" PARENT_SCOPE)