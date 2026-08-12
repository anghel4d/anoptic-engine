#include <anoptic_log.h>

void invalid_log_argument_type(void)
{
    ano::log(ano::Info, "integer: %d", "not an integer");
}
