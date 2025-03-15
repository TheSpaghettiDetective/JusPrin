// Type definitions for Config system

import { Vec2d, Vec3d } from './geometry';

export enum ConfigOptionType {
    Float,
    Floats,
    Int,
    Ints,
    String,
    Strings,
    Percent,
    Percents,
    FloatOrPercent,
    FloatsOrPercents,
    Point,
    Points,
    Point3,
    Bool,
    Bools,
    Enum,
    Enums
}

export enum ForwardCompatibilitySubstitutionRule {
    Disable,
    Enable,
    EnableSilent,
    EnableSystemSilent
}

export enum DeserializationResult {
    Failed,
    Success,
    Substituted
}

export enum DeserializationSubstitution {
    DefaultsToFalse,
    DefaultsToTrue
}

export enum SupportMaterialStyle {
    None,
    Original,
    Tree,
    TreeHybrid
}

export interface VectorConfigOption extends ConfigOption {
    values: any[];
}

export interface ConfigOption {
    type: ConfigOptionType;
    clone(): ConfigOption;
    deserialize(str: string, append?: boolean): boolean;
    serialize(): string;
    set(other: ConfigOption): void;
    nullable(): boolean;
}

export interface ConfigOptionDef {
    type: ConfigOptionType;
    default_value?: ConfigOption;
    aliases: string[];
    shortcut: string[];
    enum_values?: string[];
    enum_keys_map?: Map<string, number>;
    nullable?: boolean;
    ratio_over?: string;
    createDefaultOption(): ConfigOption;
}

export interface ConfigDef {
    options: Map<string, ConfigOptionDef>;
    get(key: string): ConfigOptionDef | null;
}

export interface ConfigSubstitution {
    optDef: ConfigOptionDef;
    oldValue: string;
    newValue: ConfigOption;
}

export interface ConfigSubstitutionContext {
    rule: ForwardCompatibilitySubstitutionRule;
    substitutions: ConfigSubstitution[];
    unrecognizedKeys: string[];
}

export class UnknownOptionException extends Error {
    constructor(optKey: string) {
        super(`Unknown option: ${optKey}`);
        this.name = 'UnknownOptionException';
    }
}

// Helper class for config operations
export class ConfigHelpers {
    static enumLooksLikeTrueValue(value: string): boolean {
        const trueValues = ['true', '1', 'yes', 'on'];
        return trueValues.includes(value.toLowerCase());
    }
}

// Concrete config option implementations
export class ConfigOptionFloat implements ConfigOption {
    value: number = 0;
    type: ConfigOptionType = ConfigOptionType.Float;

    clone(): ConfigOption {
        const copy = new ConfigOptionFloat();
        copy.value = this.value;
        return copy;
    }

    deserialize(str: string): boolean {
        const num = parseFloat(str);
        if (isNaN(num)) return false;
        this.value = num;
        return true;
    }

    serialize(): string {
        return this.value.toString();
    }

    set(other: ConfigOption): void {
        if (other instanceof ConfigOptionFloat) {
            this.value = other.value;
        }
    }

    nullable(): boolean {
        return false;
    }
}

export class ConfigOptionInt implements ConfigOption {
    value: number = 0;
    type: ConfigOptionType = ConfigOptionType.Int;

    clone(): ConfigOption {
        const copy = new ConfigOptionInt();
        copy.value = this.value;
        return copy;
    }

    deserialize(str: string): boolean {
        const num = parseInt(str, 10);
        if (isNaN(num)) return false;
        this.value = num;
        return true;
    }

    serialize(): string {
        return this.value.toString();
    }

    set(other: ConfigOption): void {
        if (other instanceof ConfigOptionInt) {
            this.value = other.value;
        }
    }

    nullable(): boolean {
        return false;
    }
}

export class ConfigOptionBool implements ConfigOption {
    value: boolean = false;
    type: ConfigOptionType = ConfigOptionType.Bool;

    clone(): ConfigOption {
        const copy = new ConfigOptionBool();
        copy.value = this.value;
        return copy;
    }

    deserialize(str: string): boolean {
        const lowerStr = str.toLowerCase();
        if (['true', '1', 'yes', 'on'].includes(lowerStr)) {
            this.value = true;
            return true;
        }
        if (['false', '0', 'no', 'off'].includes(lowerStr)) {
            this.value = false;
            return true;
        }
        return false;
    }

    serialize(): string {
        return this.value ? 'true' : 'false';
    }

    set(other: ConfigOption): void {
        if (other instanceof ConfigOptionBool) {
            this.value = other.value;
        }
    }

    nullable(): boolean {
        return false;
    }
}

export class ConfigOptionBools implements VectorConfigOption {
    values: boolean[] = [];
    type: ConfigOptionType = ConfigOptionType.Bools;

    clone(): ConfigOption {
        const copy = new ConfigOptionBools();
        copy.values = [...this.values];
        return copy;
    }

    deserialize(str: string, append: boolean = false): boolean {
        if (!append) {
            this.values = [];
        }
        const value = str.toLowerCase() === 'true' || str === '1';
        this.values.push(value);
        return true;
    }

    deserializeWithSubstitutions(str: string, append: boolean, defaultValue: DeserializationSubstitution): DeserializationResult {
        const success = this.deserialize(str, append);
        if (!success) return DeserializationResult.Failed;
        return DeserializationResult.Success;
    }

    serialize(): string {
        return this.values.map(v => v ? 'true' : 'false').join(',');
    }

    set(other: ConfigOption): void {
        if (other instanceof ConfigOptionBools) {
            this.values = [...other.values];
        }
    }

    nullable(): boolean {
        return false;
    }
}

export class ConfigOptionBoolsNullable extends ConfigOptionBools {
    nullable(): boolean {
        return true;
    }

    deserializeWithSubstitutions(str: string, append: boolean, defaultValue: DeserializationSubstitution): DeserializationResult {
        if (str.toLowerCase() === 'nil') {
            if (!append) {
                this.values = [];
            }
            this.values.push(defaultValue === DeserializationSubstitution.DefaultsToTrue);
            return DeserializationResult.Substituted;
        }
        return super.deserializeWithSubstitutions(str, append, defaultValue);
    }
}

export class ConfigOptionPercent implements ConfigOption {
    value: number = 0;
    type: ConfigOptionType = ConfigOptionType.Percent;

    clone(): ConfigOption {
        const copy = new ConfigOptionPercent();
        copy.value = this.value;
        return copy;
    }

    deserialize(str: string): boolean {
        if (str.endsWith('%')) {
            const num = parseFloat(str.slice(0, -1));
            if (isNaN(num)) return false;
            this.value = num / 100;
        } else {
            const num = parseFloat(str);
            if (isNaN(num)) return false;
            this.value = num;
        }
        return true;
    }

    serialize(): string {
        return `${this.value * 100}%`;
    }

    set(other: ConfigOption): void {
        if (other instanceof ConfigOptionPercent) {
            this.value = other.value;
        }
    }

    nullable(): boolean {
        return false;
    }

    getAbsValue(ratio_over: number): number {
        return this.value * ratio_over;
    }
}

export class ConfigOptionFloatOrPercent implements ConfigOption {
    value: number = 0;
    percent: boolean = false;
    type: ConfigOptionType = ConfigOptionType.FloatOrPercent;

    clone(): ConfigOption {
        const copy = new ConfigOptionFloatOrPercent();
        copy.value = this.value;
        copy.percent = this.percent;
        return copy;
    }

    deserialize(str: string): boolean {
        if (str.endsWith('%')) {
            const num = parseFloat(str.slice(0, -1));
            if (isNaN(num)) return false;
            this.value = num / 100;
            this.percent = true;
        } else {
            const num = parseFloat(str);
            if (isNaN(num)) return false;
            this.value = num;
            this.percent = false;
        }
        return true;
    }

    serialize(): string {
        return this.percent ? `${this.value * 100}%` : this.value.toString();
    }

    set(other: ConfigOption): void {
        if (other instanceof ConfigOptionFloatOrPercent) {
            this.value = other.value;
            this.percent = other.percent;
        }
    }

    nullable(): boolean {
        return false;
    }

    getAbsValue(ratio_over: number): number {
        return this.percent ? this.value * ratio_over : this.value;
    }
}

export class ConfigOptionString implements ConfigOption {
    value: string = '';
    type: ConfigOptionType = ConfigOptionType.String;

    clone(): ConfigOption {
        const copy = new ConfigOptionString();
        copy.value = this.value;
        return copy;
    }

    deserialize(str: string): boolean {
        this.value = str;
        return true;
    }

    serialize(): string {
        return this.value;
    }

    set(other: ConfigOption): void {
        if (other instanceof ConfigOptionString) {
            this.value = other.value;
        }
    }

    nullable(): boolean {
        return false;
    }
}

export class ConfigOptionPoint implements ConfigOption {
    value: Vec2d = new Vec2d();
    type: ConfigOptionType = ConfigOptionType.Point;

    clone(): ConfigOption {
        const copy = new ConfigOptionPoint();
        copy.value = this.value.clone();
        return copy;
    }

    deserialize(str: string): boolean {
        const parts = str.split(',');
        if (parts.length !== 2) return false;
        const x = parseFloat(parts[0]);
        const y = parseFloat(parts[1]);
        if (isNaN(x) || isNaN(y)) return false;
        this.value = new Vec2d(x, y);
        return true;
    }

    serialize(): string {
        return this.value.toString();
    }

    set(other: ConfigOption): void {
        if (other instanceof ConfigOptionPoint) {
            this.value = other.value.clone();
        }
    }

    nullable(): boolean {
        return false;
    }
}

export class ConfigOptionPoint3 implements ConfigOption {
    value: Vec3d = new Vec3d();
    type: ConfigOptionType = ConfigOptionType.Point3;

    clone(): ConfigOption {
        const copy = new ConfigOptionPoint3();
        copy.value = this.value.clone();
        return copy;
    }

    deserialize(str: string): boolean {
        const parts = str.split(',');
        if (parts.length !== 3) return false;
        const x = parseFloat(parts[0]);
        const y = parseFloat(parts[1]);
        const z = parseFloat(parts[2]);
        if (isNaN(x) || isNaN(y) || isNaN(z)) return false;
        this.value = new Vec3d(x, y, z);
        return true;
    }

    serialize(): string {
        return this.value.toString();
    }

    set(other: ConfigOption): void {
        if (other instanceof ConfigOptionPoint3) {
            this.value = other.value.clone();
        }
    }

    nullable(): boolean {
        return false;
    }
}

export class ConfigOptionStrings implements VectorConfigOption {
    values: string[] = [];
    type: ConfigOptionType = ConfigOptionType.Strings;

    clone(): ConfigOption {
        const copy = new ConfigOptionStrings();
        copy.values = [...this.values];
        return copy;
    }

    deserialize(str: string, append: boolean = false): boolean {
        if (!append) {
            this.values = [];
        }
        this.values.push(str);
        return true;
    }

    serialize(): string {
        return this.values.join(';');
    }

    set(other: ConfigOption): void {
        if (other instanceof ConfigOptionStrings) {
            this.values = [...other.values];
        }
    }

    nullable(): boolean {
        return false;
    }
}

// Add other config option implementations as needed...

// Type declarations for external modules
export interface JsonType {
    [key: string]: any;
}

export interface BoostType {
    property_tree: {
        ptree: any;
    };
}