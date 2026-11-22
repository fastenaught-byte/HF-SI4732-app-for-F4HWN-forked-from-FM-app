#include "app_api.h"

__attribute__((section(".text.entry"), used))
void app_main(const app_api_t *api)
{
    if (!api) return;

    api->display_clear();
    api->status_clear();
    api->print_string("SSB PATCH DATA", 0, 127, 2, 8);
    api->print_string("EXIT TO RETURN", 0, 127, 3, 8);
    api->blit_status();
    api->blit_full();
    while (api->get_key() != APP_KEY_EXIT) api->delay_ms(10);
}
