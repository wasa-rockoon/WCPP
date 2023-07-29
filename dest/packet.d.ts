export type Char = string;
export type Version = 1 | 2 | 3 | 4;
export declare class Payload {
    raw: ArrayBuffer;
    view: DataView;
    size: number;
    constructor();
    get int8(): number;
    get int16(): number;
    get int32(): number;
    get uint8(): number;
    get uint16(): number;
    get uint32(): number;
    get float16(): number;
    get float32(): number;
    set int8(value: number);
    set int16(value: number);
    set int32(value: number);
    set uint8(value: number);
    set uint16(value: number);
    set uint32(value: number);
    set float16(value: number);
    set float32(value: number);
}
export declare class Entry {
    type: Char;
    payload: Payload;
    constructor();
    encode(view: DataView): number;
    decode(view: DataView, version?: Version): number;
    formatNumber(f: any): number;
    format(f: any): string;
}
declare const Kind: {
    readonly COMMAND: 0;
    readonly TELEMETRY: 1;
};
type Kind = typeof Kind[keyof typeof Kind];
export declare class Packet {
    kind: Kind;
    id?: Char;
    from: number;
    node: number;
    dest: number;
    size: number;
    seq: number;
    entries: Entry[];
    constructor(kind: Kind, id: Char | undefined, from: number, node: number, dest: number, size: number, seq: number);
    get(type: Char, index?: number): Entry | undefined;
    getTime(): number | undefined;
    encode(view: DataView): number;
    static decode(view: DataView, version?: Version): Packet;
    static decodeLogFile(file: File, version?: Version): Promise<Packet[]>;
}
export declare function readFileAsArrayBuffer(file: File): Promise<ArrayBuffer>;
export declare function decodeCOBS<A>(buffer: ArrayBuffer, map: (buffer: ArrayBuffer) => A): A[];
export {};
//# sourceMappingURL=packet.d.ts.map