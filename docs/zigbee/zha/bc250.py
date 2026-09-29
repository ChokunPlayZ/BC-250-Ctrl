"""Optional BC-250 PSU sensors for ZHA. Installation: docs/ZIGBEE.md."""

from zigpy.zcl.clusters.general import AnalogInput
from zigpy.zcl.clusters.homeautomation import ElectricalMeasurement

from zhaquirks.builder import (
    BinarySensorDeviceClass,
    QuirkBuilder,
    SensorDeviceClass,
    SensorStateClass,
)
from zhaquirks.clusters import CustomCluster


def scaled_sample(divisor, invalid):
    """Keep the firmware's unavailable sentinel out of numeric sensor history."""
    return lambda value: None if value == invalid else value / divisor


class BC250FanInput(CustomCluster, AnalogInput):
    """Hide a cached fan value until StatusFlags confirms a valid PSU sample."""

    _fan_raw = None

    def _update_attribute(self, attrid, value):
        if attrid == AnalogInput.AttributeDefs.present_value.id:
            self._fan_raw = value
            flags = self.get(AnalogInput.AttributeDefs.status_flags.name)
            value = value if flags is not None and not (flags & 0x02) else None
        super()._update_attribute(attrid, value)
        if attrid == AnalogInput.AttributeDefs.status_flags.id:
            super()._update_attribute(
                AnalogInput.AttributeDefs.present_value.id,
                self._fan_raw if value is not None and not (value & 0x02) else None,
            )


def has_psu_clusters(device):
    """Power-only firmware keeps its standard ZHA switch without this quirk."""
    endpoint = device.endpoints.get(1)
    return (
        endpoint is not None
        and endpoint.profile_id == 0x0104
        and endpoint.device_type == 0x0002
        and ElectricalMeasurement.cluster_id in endpoint.in_clusters
        and AnalogInput.cluster_id in endpoint.in_clusters
    )


builder = (
    QuirkBuilder("BC250", "BC250 Controller")
    .filter(has_psu_clusters)
    .replaces(BC250FanInput, endpoint_id=1)
    # Avoid duplicate generic sensors and a writable Analog Input number entity.
    .prevent_default_entity_creation(endpoint_id=1, cluster_id=ElectricalMeasurement.cluster_id)
    .prevent_default_entity_creation(endpoint_id=1, cluster_id=AnalogInput.cluster_id)
)

for attribute, name, divisor, invalid, device_class, unit, precision in (
    ("rms_voltage", "PSU input voltage", 10, 0xFFFF, SensorDeviceClass.VOLTAGE, "V", 1),
    ("rms_current", "PSU input current", 100, 0xFFFF, SensorDeviceClass.CURRENT, "A", 2),
    ("dc_voltage", "PSU output voltage", 100, -32768, SensorDeviceClass.VOLTAGE, "V", 2),
    ("dc_current", "PSU output current", 10, -32768, SensorDeviceClass.CURRENT, "A", 1),
):
    builder.sensor(
        attribute_name=attribute,
        cluster_id=ElectricalMeasurement.cluster_id,
        endpoint_id=1,
        attribute_converter=scaled_sample(divisor, invalid),
        device_class=device_class,
        state_class=SensorStateClass.MEASUREMENT,
        unit=unit,
        suggested_display_precision=precision,
        translation_key=name.lower().replace(" ", "_"),
        fallback_name=name,
    )

(
    builder.sensor(
        attribute_name="present_value",
        cluster_id=AnalogInput.cluster_id,
        endpoint_id=1,
        state_class=SensorStateClass.MEASUREMENT,
        suggested_display_precision=0,
        translation_key="psu_fan_raw",
        fallback_name="PSU fan raw",
    )
    .binary_sensor(
        attribute_name="status_flags",
        cluster_id=AnalogInput.cluster_id,
        endpoint_id=1,
        attribute_converter=lambda flags: bool(flags & 0x02),
        device_class=BinarySensorDeviceClass.PROBLEM,
        translation_key="psu_telemetry_fault",
        fallback_name="PSU telemetry fault",
    )
    .add_to_registry()
)
