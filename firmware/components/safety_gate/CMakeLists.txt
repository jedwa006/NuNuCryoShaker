idf_component_register(
    SRCS "safety_gate.c"
    INCLUDE_DIRS "include"
    REQUIRES nvs_flash pid_controller machine_state session_mgr
)
