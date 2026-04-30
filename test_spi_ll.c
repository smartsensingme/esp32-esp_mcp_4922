#include "esp_err.h"
#include "hal/spi_ll.h"
#include "soc/spi_struct.h"

void test_ll() {
    spi_dev_t *hw = &GPSPI2;
    spi_ll_user_start(hw);
}
