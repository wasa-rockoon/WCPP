
export type Char = string

export type Version = 1 | 2 | 3 | 4

export class Payload {
    raw: ArrayBuffer
    view: DataView
    size: number

    constructor() {
        this.raw = new ArrayBuffer(4)
        this.view = new DataView(this.raw)
        this.size = 0
    }

    get int8(): number {
        return this.view.getInt8(0)
    }
    get int16(): number {
        return this.view.getInt16(0, true)
    }
    get int32(): number {
        return this.view.getInt32(0, true)
    }
    get uint8(): number {
        return this.view.getUint8(0)
    }
    get uint16(): number {
        return this.view.getUint16(0, true)
    }
    get uint32(): number {
        return this.view.getUint32(0, true)
    }
    get float16(): number {
        const value16: number = this.view.getUint16(0, true)
        let value32: number

        const sign: number = value16 >> 15
        const exp: number = (value16 >> 10) & 0x1F
        const frac: number = value16 & 0x03FF

        if (exp == 0) {
            if (frac == 0) value32 = sign << 31 // +- Zero
            else value32 = (sign << 31) | (frac << 13) // Denormalized values
        }
        else if (exp == 0x1F) {
            if (frac == 0) value32 = (sign << 31) | 0x7F800000 // +- Infinity
            else value32 = (sign << 31) | (0xFF << 23) | (frac << 13) // (S/Q)NaN
        }
        // Normalized values
        else value32 = (sign << 31) | ((exp - 15 + 127) << 23) | (frac << 13)

        const buffer = new ArrayBuffer(4)
        const view = new DataView(buffer)
        view.setUint32(0, value32, true)
        return view.getFloat32(0, true)
    }
    get float32(): number {
        return this.view.getFloat32(0, true)
    }
    set int8(value: number) {
        this.view.setUint32(0, 0, true)
        this.view.setInt8(0, value)
    }
    set int16(value: number) {
        this.view.setUint32(0, 0, true)
        this.view.setInt16(0, value, true)
    }
    set int32(value: number) {
        this.view.setInt32(0, value, true)
    }
    set uint8(value: number) {
        this.view.setUint32(0, 0, true)
        this.view.setUint8(0, value)
    }
    set uint16(value: number) {
        this.view.setUint32(0, 0, true)
        this.view.setUint16(0, value, true)
    }

    set uint32(value: number) {
        this.view.setUint32(0, value, true)
    }
    set float16(value: number) {
        const buffer = new ArrayBuffer(4)
        const view = new DataView(buffer)
        view.setFloat32(0, value, true)
        const value32: number = view.getUint32(0, true)
        let value16: number

        const sign: number = value32 >> 31
        const exp: number = (value32 >> 23) & 0xFF
        const frac: number = value32 & 0x007FFFFF

        if (exp == 0) {
            if (frac == 0) value16 = sign << 15 // +- Zero
            else value16 = (sign << 15) | (frac >> 13) // Denormalized values
        }
        else if (exp == 0xFF) {
            if (frac == 0) value16 = (sign << 15) | 0x7C0 // +- Infinity
            else value16 = (sign << 15) | (0x1F << 20) | (frac >> 13) // (S/Q)NaN
        }
        // Normalized values
        else value16 = (sign << 15) |
            (Math.max(Math.min(exp - 127 + 15, 0), 31) << 23) | (frac << 13)

        view.setUint16(0, value16, true)
    }
    set float32(value: number) {
        this.view.setFloat32(0, value, true)
    }
}

export class Entry {
    type: Char
    payload: Payload

    constructor() {
        this.type = '@'
        this.payload = new Payload()
    }

    encode(view: DataView): number {
        let len

        const raw = this.payload.view.getUint32(0, true)
        let type = (this.type.charCodeAt(0) - 64) & 0b00111111

        if (raw & 0xFFFF0000) {
            len = 4
            type |= 0b11000000
            view.setUint32(1, raw, true)
        }
        else if (raw & 0xFF00) {
            len = 2
            type |= 0b10000000
            view.setUint16(1, raw, true)
        }
        else if (raw & 0xFF) {
            len = 1
            type |= 0b01000000
            view.setUint8(1, raw)
        }
        else {
            len = 0
        }

        view.setUint8(0, type)

        return len + 1
    }

    decode(view: DataView, version: Version = 4): number {
        let len;
        if (version <= 3) {
            this.type = String.fromCharCode(view.getUint8(0) & 0b01111111)
            if (view.getUint8(0) & 0b10000000) len = 0
            else len = 4
        }
        else {
            this.type = String.fromCharCode(
                (view.getUint8(0) & 0b00111111) + 64)
            switch (view.getUint8(0) & 0b11000000) {
                case 0b01000000:
                    len = 1
                    break
                case 0b10000000:
                    len = 2
                    break
                case 0b11000000:
                    len = 4
                    break
                default:
                    len = 0
            }
        }

        switch (len) {
            case 1:
                this.payload.view.setUint8(0, view.getUint8(1))
                break
            case 2:
                this.payload.view.setUint16(0, view.getUint16(1, true), true)
                break
            case 4:
                this.payload.view.setUint32(0, view.getUint32(1, true), true)
                break
        }
        this.payload.size = len

        return 1 + len
    }

    formatNumber(f: any): number {
        const value: number = (this.payload as any)[f.datatype]
        if (f.formatNumber) return f.formatNumber(value)
        else if (f.format) return Number(f.format(value))
        else return value
    }

    format(f: any): string {
        const value: number = (this.payload as any)[f.datatype]
        if (f.format) return f.format(value)
        else if (f.datatype == 'float16') return value.toPrecision(3)
        else if (f.datatype == 'float32') return value.toPrecision(7)
        else return String(value)
    }
}

const Kind = {
    COMMAND: 0,
    TELEMETRY: 1,
} as const

type Kind = typeof Kind[keyof typeof Kind]

export class Packet {
    kind: Kind
    id?: Char
    from: number
    node: number
    dest: number
    size: number
    seq: number
    entries: Entry[]

    constructor(kind: Kind, id: Char | undefined, from: number, node: number,
                dest: number, size: number, seq: number) {
        this.kind = kind
        this.id = id
        this.from = from
        this.node = node
        this.dest = dest
        this.size = size
        this.seq  = seq
        this.entries = []

    }

    get(type: Char, index: number = 0): Entry | undefined {
        let i = 0;
        let prev_type = ''
        for (const entry of this.entries) {
            if (entry.type == prev_type) i++
            else i = 0

            if (entry.type == type && i == index) return entry

            prev_type = entry.type
        }
        return undefined
    }

    getTime(): number | undefined {
        if (this.entries[this.size - 1]?.type == 't') {
            return this.entries[this.size - 1]?.payload.uint32
        }
        else return undefined
    }

    encode(view: DataView): number {
        view.setUint8(
            0,
            (this.kind << 7)
                | (this.id ? this.id.charCodeAt(0) & 0b01111111 : 0))
        view.setUint8(1, ((this.from & 0b111) << 5) | (this.node & 0b11111))
        view.setUint8(2, ((this.dest & 0b111) << 5) | (this.size & 0b11111))
        view.setUint8(3, this.seq)

        let i = 4;

        for (const entry of this.entries) {
            const buffer = new ArrayBuffer(5)
            const entry_view = new DataView(buffer)
            const len = entry.encode(entry_view)

            view.setUint8(i, entry_view.getUint8(0))
            view.setUint32(i + 1, entry_view.getUint32(1, true), true)

            i += len
        }
        return i
    }

    static decode(view: DataView, version: Version = 4): Packet {
        let packet
        let i
        let id, from, size
        let kind: Kind = Kind.TELEMETRY, node = 0, dest = 0, seq = 0


        if (version <= 3) {
            id = view.getUint8(0) ?
                String.fromCharCode(view.getUint8(0)) : undefined
            from = view.getUint8(1)
            size = view.getUint8(2)
            i = 3
        }
        else {
            kind = view.getUint8(0) >> 7 ? Kind.TELEMETRY : Kind.COMMAND
            id   = view.getUint8(0) & 0b01111111 ?
                String.fromCharCode(view.getUint8(0) & 0b01111111) : undefined
            from = view.getUint8(1) >> 5
            node = view.getUint8(1) & 0b11111
            dest = view.getUint8(2) >> 5
            size = view.getUint8(2) & 0b11111
            seq  = view.getUint8(3)

            i = 4;
        }


        packet = new Packet(kind, id, from, node, dest, size, seq)

        for (let n = 0; n < size; n++) {
            const buffer = new ArrayBuffer(5)
            const entry_view = new DataView(buffer)
            entry_view.setUint8(0, view.getUint8(i))
            entry_view.setUint32(1, view.getUint32(i + 1, true), true)
            const entry = new Entry()
            i += entry.decode(entry_view, version)
            packet.entries.push(entry)
        }

        return packet
    }

    static async decodeLogFile(file: File, version: Version = 3)
    : Promise<Packet[]> {
        const buffer = await readFileAsArrayBuffer(file)

        if (version < 2) {
            return []
        }
        else {
            return decodeCOBS(buffer, (buf) => {
                return Packet.decode(new DataView(buf), version)
            })
        }
    }
}


export function readFileAsArrayBuffer(file: File): Promise<ArrayBuffer> {
    return new Promise((resolve, reject) => {
        const reader = new FileReader();
        reader.onload = () => {
            resolve(reader.result as ArrayBuffer);
        };
        reader.onerror = reject;
        reader.readAsArrayBuffer(file);
    })
}

export function decodeCOBS<A>(buffer: ArrayBuffer, map: (buffer: ArrayBuffer) => A): A[] {
    const result = []

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
            const zero = next_zero != 0xFF

            next_zero = encoded[src];
            rest = next_zero;
            if (next_zero == 0) {
                if (dest > 3) result.push(map(decoded_buf))

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

    return result
}
