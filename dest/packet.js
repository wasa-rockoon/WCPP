"use strict";
var __awaiter = (this && this.__awaiter) || function (thisArg, _arguments, P, generator) {
    function adopt(value) { return value instanceof P ? value : new P(function (resolve) { resolve(value); }); }
    return new (P || (P = Promise))(function (resolve, reject) {
        function fulfilled(value) { try { step(generator.next(value)); } catch (e) { reject(e); } }
        function rejected(value) { try { step(generator["throw"](value)); } catch (e) { reject(e); } }
        function step(result) { result.done ? resolve(result.value) : adopt(result.value).then(fulfilled, rejected); }
        step((generator = generator.apply(thisArg, _arguments || [])).next());
    });
};
Object.defineProperty(exports, "__esModule", { value: true });
exports.decodeCOBS = exports.readFileAsArrayBuffer = exports.Packet = exports.Entry = exports.Payload = void 0;
class Payload {
    constructor() {
        this.raw = new ArrayBuffer(4);
        this.view = new DataView(this.raw);
        this.size = 0;
    }
    get int8() {
        return this.view.getInt8(0);
    }
    get int16() {
        return this.view.getInt16(0, true);
    }
    get int32() {
        return this.view.getInt32(0, true);
    }
    get uint8() {
        return this.view.getUint8(0);
    }
    get uint16() {
        return this.view.getUint16(0, true);
    }
    get uint32() {
        return this.view.getUint32(0, true);
    }
    get float16() {
        const value16 = this.view.getUint16(0, true);
        let value32;
        const sign = value16 >> 15;
        const exp = (value16 >> 10) & 0x1F;
        const frac = value16 & 0x03FF;
        if (exp == 0) {
            if (frac == 0)
                value32 = sign << 31; // +- Zero
            else
                value32 = (sign << 31) | (frac << 13); // Denormalized values
        }
        else if (exp == 0x1F) {
            if (frac == 0)
                value32 = (sign << 31) | 0x7F800000; // +- Infinity
            else
                value32 = (sign << 31) | (0xFF << 23) | (frac << 13); // (S/Q)NaN
        }
        // Normalized values
        else
            value32 = (sign << 31) | ((exp - 15 + 127) << 23) | (frac << 13);
        const buffer = new ArrayBuffer(4);
        const view = new DataView(buffer);
        view.setUint32(0, value32, true);
        return view.getFloat32(0, true);
    }
    get float32() {
        return this.view.getFloat32(0, true);
    }
    set int8(value) {
        this.view.setUint32(0, 0, true);
        this.view.setInt8(0, value);
    }
    set int16(value) {
        this.view.setUint32(0, 0, true);
        this.view.setInt16(0, value, true);
    }
    set int32(value) {
        this.view.setInt32(0, value, true);
    }
    set uint8(value) {
        this.view.setUint32(0, 0, true);
        this.view.setUint8(0, value);
    }
    set uint16(value) {
        this.view.setUint32(0, 0, true);
        this.view.setUint16(0, value, true);
    }
    set uint32(value) {
        this.view.setUint32(0, value, true);
    }
    set float16(value) {
        const buffer = new ArrayBuffer(4);
        const view = new DataView(buffer);
        view.setFloat32(0, value, true);
        const value32 = view.getUint32(0, true);
        let value16;
        const sign = value32 >> 31;
        const exp = (value32 >> 23) & 0xFF;
        const frac = value32 & 0x007FFFFF;
        if (exp == 0) {
            if (frac == 0)
                value16 = sign << 15; // +- Zero
            else
                value16 = (sign << 15) | (frac >> 13); // Denormalized values
        }
        else if (exp == 0xFF) {
            if (frac == 0)
                value16 = (sign << 15) | 0x7C0; // +- Infinity
            else
                value16 = (sign << 15) | (0x1F << 20) | (frac >> 13); // (S/Q)NaN
        }
        // Normalized values
        else
            value16 = (sign << 15) |
                (Math.max(Math.min(exp - 127 + 15, 0), 31) << 23) | (frac << 13);
        view.setUint16(0, value16, true);
    }
    set float32(value) {
        this.view.setFloat32(0, value, true);
    }
}
exports.Payload = Payload;
class Entry {
    constructor() {
        this.type = '@';
        this.payload = new Payload();
    }
    encode(view) {
        let len;
        const raw = this.payload.view.getUint32(0, true);
        let type = (this.type.charCodeAt(0) - 64) & 0b00111111;
        if (raw & 0xFFFF0000) {
            len = 4;
            type |= 0b11000000;
            view.setUint32(1, raw, true);
        }
        else if (raw & 0xFF00) {
            len = 2;
            type |= 0b10000000;
            view.setUint16(1, raw, true);
        }
        else if (raw & 0xFF) {
            len = 1;
            type |= 0b01000000;
            view.setUint8(1, raw);
        }
        else {
            len = 0;
        }
        view.setUint8(0, type);
        return len + 1;
    }
    decode(view, version = 4) {
        let len;
        if (version <= 3) {
            this.type = String.fromCharCode(view.getUint8(0) & 0b01111111);
            if (view.getUint8(0) & 0b10000000)
                len = 0;
            else
                len = 4;
        }
        else {
            this.type = String.fromCharCode((view.getUint8(0) & 0b00111111) + 64);
            switch (view.getUint8(0) & 0b11000000) {
                case 0b01000000:
                    len = 1;
                    break;
                case 0b10000000:
                    len = 2;
                    break;
                case 0b11000000:
                    len = 4;
                    break;
                default:
                    len = 0;
            }
        }
        switch (len) {
            case 1:
                this.payload.view.setUint8(0, view.getUint8(1));
                break;
            case 2:
                this.payload.view.setUint16(0, view.getUint16(1, true), true);
                break;
            case 4:
                this.payload.view.setUint32(0, view.getUint32(1, true), true);
                break;
        }
        this.payload.size = len;
        return 1 + len;
    }
    formatNumber(f) {
        const value = this.payload[f.datatype];
        if (f.formatNumber)
            return f.formatNumber(value);
        else if (f.format)
            return Number(f.format(value));
        else
            return value;
    }
    format(f) {
        const value = this.payload[f.datatype];
        if (f.format)
            return f.format(value);
        else if (f.datatype == 'float16')
            return value.toPrecision(3);
        else if (f.datatype == 'float32')
            return value.toPrecision(7);
        else
            return String(value);
    }
}
exports.Entry = Entry;
const Kind = {
    COMMAND: 0,
    TELEMETRY: 1,
};
class Packet {
    constructor(kind, id, from, node, dest, size, seq) {
        this.kind = kind;
        this.id = id;
        this.from = from;
        this.node = node;
        this.dest = dest;
        this.size = size;
        this.seq = seq;
        this.entries = [];
    }
    get(type, index = 0) {
        let i = 0;
        let prev_type = '';
        for (const entry of this.entries) {
            if (entry.type == prev_type)
                i++;
            else
                i = 0;
            if (entry.type == type && i == index)
                return entry;
            prev_type = entry.type;
        }
        return undefined;
    }
    getTime() {
        var _a, _b;
        if (((_a = this.entries[this.size - 1]) === null || _a === void 0 ? void 0 : _a.type) == 't') {
            return (_b = this.entries[this.size - 1]) === null || _b === void 0 ? void 0 : _b.payload.uint32;
        }
        else
            return undefined;
    }
    encode(view) {
        view.setUint8(0, (this.kind << 7)
            | (this.id ? this.id.charCodeAt(0) & 0b01111111 : 0));
        view.setUint8(1, ((this.from & 0b111) << 5) | (this.node & 0b11111));
        view.setUint8(2, ((this.dest & 0b111) << 5) | (this.size & 0b11111));
        view.setUint8(3, this.seq);
        let i = 4;
        for (const entry of this.entries) {
            const buffer = new ArrayBuffer(5);
            const entry_view = new DataView(buffer);
            const len = entry.encode(entry_view);
            view.setUint8(i, entry_view.getUint8(0));
            view.setUint32(i + 1, entry_view.getUint32(1, true), true);
            i += len;
        }
        return i;
    }
    static decode(view, version = 4) {
        let packet;
        let i;
        let id, from, size;
        let kind = Kind.TELEMETRY, node = 0, dest = 0, seq = 0;
        if (version <= 3) {
            id = view.getUint8(0) ?
                String.fromCharCode(view.getUint8(0)) : undefined;
            from = view.getUint8(1);
            size = view.getUint8(2);
            i = 3;
        }
        else {
            kind = view.getUint8(0) >> 7 ? Kind.TELEMETRY : Kind.COMMAND;
            id = view.getUint8(0) & 0b01111111 ?
                String.fromCharCode(view.getUint8(0) & 0b01111111) : undefined;
            from = view.getUint8(1) >> 5;
            node = view.getUint8(1) & 0b11111;
            dest = view.getUint8(2) >> 5;
            size = view.getUint8(2) & 0b11111;
            seq = view.getUint8(3);
            i = 4;
        }
        packet = new Packet(kind, id, from, node, dest, size, seq);
        for (let n = 0; n < size; n++) {
            const buffer = new ArrayBuffer(5);
            const entry_view = new DataView(buffer);
            entry_view.setUint8(0, view.getUint8(i));
            entry_view.setUint32(1, view.getUint32(i + 1, true), true);
            const entry = new Entry();
            i += entry.decode(entry_view, version);
            packet.entries.push(entry);
        }
        return packet;
    }
    static decodeLogFile(file, version = 3) {
        return __awaiter(this, void 0, void 0, function* () {
            const buffer = yield readFileAsArrayBuffer(file);
            if (version < 2) {
                return [];
            }
            else {
                return decodeCOBS(buffer, (buf) => {
                    return Packet.decode(new DataView(buf), version);
                });
            }
        });
    }
}
exports.Packet = Packet;
function readFileAsArrayBuffer(file) {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => {
            resolve(reader.result);
        };
        reader.onerror = reject;
        reader.readAsArrayBuffer(file);
    });
}
exports.readFileAsArrayBuffer = readFileAsArrayBuffer;
function decodeCOBS(buffer, map) {
    const result = [];
    const len = buffer.byteLength;
    const encoded = new Uint8Array(buffer);
    const decoded_buf = new ArrayBuffer(256);
    const decoded = new Uint8Array(decoded_buf, 0, 256);
    let next_zero = 0xFF;
    let src = 0;
    let rest = 0;
    let dest = 0;
    for (; src < len; src++, rest--) {
        if (rest == 0) {
            const zero = next_zero != 0xFF;
            next_zero = encoded[src];
            rest = next_zero;
            if (next_zero == 0) {
                if (dest > 3)
                    result.push(map(decoded_buf));
                dest = 0;
                next_zero = 0xFF;
                rest = 1;
            }
            else if (zero) {
                decoded[dest++] = 0;
            }
        }
        else {
            decoded[dest++] = encoded[src];
        }
    }
    return result;
}
exports.decodeCOBS = decodeCOBS;
//# sourceMappingURL=packet.js.map