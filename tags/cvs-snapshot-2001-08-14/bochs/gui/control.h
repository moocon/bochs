enum {
  BX_CPANEL_START_MAIN,
  BX_CPANEL_START_MENU,
  BX_CPANEL_START_OPTS,
  BX_CPANEL_START_OPTS_MEM,
  BX_CPANEL_START_OPTS_INTERFACE,
  BX_CPANEL_START_OPTS_DISK,
  BX_CPANEL_START_OPTS_SOUND,
  BX_CPANEL_START_OPTS_MISC,
  BX_CPANEL_RUNTIME,
  BX_CPANEL_N_MENUS
};

/* special return value from bx_control_panl */
#define BX_DISABLE_CONTROL_PANEL 10 

int bx_control_panel (int menu);
