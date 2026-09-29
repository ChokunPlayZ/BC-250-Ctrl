#include <assert.h>
#include <stdio.h>
#include <string.h>
#include "hp_fru_fixture.h"
#include "core/hp_commonslot_identity.h"

int main(void)
{
    uint8_t data[256];
    bc250_hp_commonslot_identity_t info;
    hp_fru_fixture(data);
    assert(bc250_hp_commonslot_decode_identity(data, sizeof(data), &info));
    assert(!strcmp(info.manufacturer, "DELTA"));
    assert(!strcmp(info.product_name, "HP PROLIANT SERVER PS"));
    assert(!strcmp(info.part_number, "437572-B21"));
    assert(!strcmp(info.revision, "01"));
    assert(!strcmp(info.serial_number, "5AMJQ0D4DXO3LU"));
    assert(!strcmp(info.board_part_number, "441830-001"));
    assert(info.rated_capacity_w == 1200);

    data[7] ^= 1;
    assert(!bc250_hp_commonslot_decode_identity(data, sizeof(data), &info));
    assert(!info.manufacturer[0] && !info.serial_number[0] && info.rated_capacity_w == 0);
    hp_fru_fixture(data); data[0] = 0xfe;
    assert(!bc250_hp_commonslot_decode_identity(data, sizeof(data), &info));
    hp_fru_fixture(data);
    assert(!bc250_hp_commonslot_decode_identity(data, 20, &info));
    assert(!bc250_hp_commonslot_decode_identity(NULL, 256, &info));
    assert(!bc250_hp_commonslot_decode_identity(data, 256, NULL));

    hp_fru_fixture(data); data[0x6f] ^= 1;
    assert(bc250_hp_commonslot_decode_identity(data, sizeof(data), &info));
    assert(!info.manufacturer[0] && !info.product_name[0] && !info.serial_number[0]);
    assert(!strcmp(info.board_part_number, "441830-001")); // Independent valid board area.
    data[3] = 0; fixture_checksum(data, 8);
    assert(!bc250_hp_commonslot_decode_identity(data, sizeof(data), &info));

    hp_fru_fixture(data); data[3] = 0; data[4] = 32; fixture_checksum(data, 8);
    assert(!bc250_hp_commonslot_decode_identity(data, sizeof(data), &info));
    hp_fru_fixture(data); data[3] = 0; fixture_checksum(data, 8);
    data[0x2b] = 0xc1; fixture_checksum(data + 0x28, 72);
    assert(!bc250_hp_commonslot_decode_identity(data, sizeof(data), &info)); // Premature terminator.
    hp_fru_fixture(data); data[3] = 0; fixture_checksum(data, 8);
    data[0x29] = 1; fixture_checksum(data + 0x28, 8);
    assert(!bc250_hp_commonslot_decode_identity(data, sizeof(data), &info)); // Field runs past area.

    hp_fru_fixture(data); data[0x2b] = 0x85; fixture_checksum(data + 0x28, 72);
    assert(bc250_hp_commonslot_decode_identity(data, sizeof(data), &info));
    assert(!info.manufacturer[0] && info.serial_number[0]); // Unsupported encoding is not misreported.
    hp_fru_fixture(data); data[0x2c] = 0x1b; fixture_checksum(data + 0x28, 72);
    assert(bc250_hp_commonslot_decode_identity(data, sizeof(data), &info) && !info.manufacturer[0]);
    hp_fru_fixture(data); data[0x2a] = 1; fixture_checksum(data + 0x28, 72);
    assert(bc250_hp_commonslot_decode_identity(data, sizeof(data), &info));
    assert(!info.product_name[0] && !strcmp(info.serial_number, "5AMJQ0D4DXO3LU"));

    hp_fru_fixture(data); data[0x75] ^= 1;
    assert(bc250_hp_commonslot_decode_identity(data, sizeof(data), &info) && !info.rated_capacity_w);
    hp_fru_fixture(data); data[0x74] ^= 1;
    assert(bc250_hp_commonslot_decode_identity(data, sizeof(data), &info) && !info.rated_capacity_w);
    hp_fru_fixture(data); data[5] = 31; fixture_checksum(data, 8);
    assert(bc250_hp_commonslot_decode_identity(data, sizeof(data), &info) && !info.rated_capacity_w);

    // Maximum legal ASCII field, and offsets relocated from the example PSU layout.
    memset(data, 0, sizeof(data)); data[0] = 1; data[4] = 1; fixture_checksum(data, 8);
    data[8] = 1; data[9] = 10; data[10] = 25; data[11] = 0xff;
    memset(data + 12, 'A', 63);
    for (unsigned i = 75; i < 81; ++i) data[i] = 0xc0;
    data[81] = 0xc1; fixture_checksum(data + 8, 80);
    assert(bc250_hp_commonslot_decode_identity(data, sizeof(data), &info));
    assert(strlen(info.manufacturer) == 63 && !info.product_name[0]);
    puts("PSU identity checksums, fields, bounds, encodings and capacity passed");
    return 0;
}
