// Copy into Zigbee2MQTT's external_converters directory. See docs/ZIGBEE.md.
import * as fz from 'zigbee-herdsman-converters/converters/fromZigbee';
import {presets as e, access as ea} from 'zigbee-herdsman-converters/lib/exposes';
import * as reporting from 'zigbee-herdsman-converters/lib/reporting';

const measurements = [
    ['rmsVoltage', 'psu_input_voltage', 10, 0xffff, 'V'],
    ['rmsCurrent', 'psu_input_current', 100, 0xffff, 'A'],
    ['dcVoltage', 'psu_output_voltage', 100, -32768, 'V'],
    ['dcCurrent', 'psu_output_current', 10, -32768, 'A'],
];

const electrical = {
    cluster: 'haElectricalMeasurement',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        const result = {};
        for (const [attribute, property, divisor, invalid] of measurements) {
            if (Object.hasOwn(msg.data, attribute)) {
                const value = msg.data[attribute];
                result[property] = value === invalid ? null : value / divisor;
            }
        }
        return result;
    },
};

// Firmware reports fan value and fault flags separately. Remember both per endpoint
// so a fault clears an earlier reading and a later valid sample can restore it.
const fanSamples = new WeakMap();
const fan = {
    cluster: 'genAnalogInput',
    type: ['attributeReport', 'readResponse'],
    convert: (model, msg) => {
        const sample = fanSamples.get(msg.endpoint) ?? {raw: null, available: false};
        if (Object.hasOwn(msg.data, 'presentValue')) sample.raw = msg.data.presentValue;
        const hasFlags = Object.hasOwn(msg.data, 'statusFlags');
        if (hasFlags) sample.available = (msg.data.statusFlags & 0x02) === 0;
        fanSamples.set(msg.endpoint, sample);
        const result = {psu_fan_raw: sample.available ? sample.raw : null};
        if (hasFlags) result.psu_available = sample.available;
        return result;
    },
};

const power = {
    key: ['state'],
    convertSet: async (entity, key, value) => {
        if (typeof value !== 'string' || !['ON', 'OFF', 'TOGGLE'].includes(value.toUpperCase())) {
            throw new Error('state must be ON, OFF, or TOGGLE');
        }
        await entity.command('genOnOff', value.toLowerCase(), {});
        // No optimistic state result: a successful command is not sensed power.
    },
    convertGet: async (entity) => {
        await entity.read('genOnOff', ['onOff']);
    },
};

const telemetry = {
    key: [...measurements.map(([, property]) => property), 'psu_fan_raw', 'psu_available'],
    convertGet: async (entity, key) => {
        const measurement = measurements.find(([, property]) => property === key);
        if (measurement) await entity.read('haElectricalMeasurement', [measurement[0]]);
        else await entity.read('genAnalogInput', ['presentValue', 'statusFlags']);
    },
};

export default {
    fingerprint: [{manufacturerName: 'CKLabs', modelID: 'BC250 Controller'}],
    model: 'BC250 Controller',
    vendor: 'CKLabs',
    description: 'BC-250 sensed power controller with optional HP Common Slot PSU telemetry',
    fromZigbee: [fz.on_off, electrical, fan],
    toZigbee: [power, telemetry],
    exposes: (device) => {
        const exposes = [e.switch()];
        // The converter library also calls this with a dummy device while loading.
        if (!device?.isDummyDevice && !device?.getEndpoint?.(1)?.supportsInputCluster('haElectricalMeasurement')) {
            return exposes;
        }
        for (const [, property, , , unit] of measurements) {
            exposes.push(e.numeric(property, ea.STATE_GET).withUnit(unit)
                .withDescription(`PSU ${property.replace('psu_', '').replaceAll('_', ' ')}`));
        }
        exposes.push(e.numeric('psu_fan_raw', ea.STATE_GET)
            .withDescription('Raw PSU PIC fan reading, not RPM; null while PSU telemetry is unavailable'));
        exposes.push(e.binary('psu_available', ea.STATE_GET, true, false)
            .withDescription('PSU I²C sample is valid; separate from Zigbee device availability'));
        return exposes;
    },
    configure: async (device, coordinatorEndpoint) => {
        const endpoint = device.getEndpoint(1);
        await reporting.bind(endpoint, coordinatorEndpoint, ['genOnOff']);
        await reporting.onOff(endpoint, {min: 0, max: 300, change: 1});
        await endpoint.read('genOnOff', ['onOff']);
        if (endpoint.supportsInputCluster('haElectricalMeasurement')) {
            // PSU reports are sent directly to coordinator endpoint 1 by firmware.
            // Do not configure generic power/energy attributes it does not implement.
            await endpoint.read('haElectricalMeasurement', measurements.map(([attribute]) => attribute));
            await endpoint.read('genAnalogInput', ['presentValue', 'statusFlags']);
        }
    },
    onEvent: async (event) => {
        if (event.type !== 'start' && event.type !== 'deviceAnnounce') return;
        const endpoint = event.data.device.getEndpoint(1);
        if (endpoint?.supportsInputCluster('genAnalogInput')) {
            // Flags are sent on validity changes, not every periodic fan report.
            // Refresh them after host restart so the in-memory cache is initialized.
            await endpoint.read('genAnalogInput', ['presentValue', 'statusFlags']);
        }
    },
};
