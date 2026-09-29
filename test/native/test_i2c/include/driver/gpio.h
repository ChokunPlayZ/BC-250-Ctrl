#pragma once
#define GPIO_IS_VALID_OUTPUT_GPIO(pin) ((pin) >= 0 && (pin) <= 28)
int gpio_get_level(int gpio);
