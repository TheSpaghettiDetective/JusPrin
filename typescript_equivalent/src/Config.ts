// TypeScript equivalent of Config.cpp
import { format, floatToStringDecimalPoint } from './format';
import { Utils } from './Utils';
import { LocalesUtils, CNumericLocalesSetter } from './LocalesUtils';
import { Vec2d, Vec3d } from './geometry';

// Type definitions (originally from Config.hpp)
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
        if (other.type !== this.type) {
            throw new Error('ConfigOptionSingle: Assigning an incompatible type');
        }
        if (!(other instanceof ConfigOptionFloat)) {
            throw new Error('ConfigOptionSingle: Assigning an incompatible type');
        }
        this.value = other.value;
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
        if (other.type !== this.type) {
            throw new Error('ConfigOptionSingle: Assigning an incompatible type');
        }
        if (!(other instanceof ConfigOptionInt)) {
            throw new Error('ConfigOptionSingle: Assigning an incompatible type');
        }
        this.value = other.value;
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
        if (other.type !== this.type) {
            throw new Error('ConfigOptionSingle: Assigning an incompatible type');
        }
        if (!(other instanceof ConfigOptionBool)) {
            throw new Error('ConfigOptionSingle: Assigning an incompatible type');
        }
        this.value = other.value;
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

// Node built-in modules
import * as fs from 'node:fs';
import * as path from 'node:path';

// Constants
const BBL_JSON_KEY_VERSION = "version";
const BBL_JSON_KEY_NAME = "name";
const BBL_JSON_KEY_URL = "url";
const BBL_JSON_KEY_TYPE = "type";
const BBL_JSON_KEY_SETTING_ID = "setting_id";
const BBL_JSON_KEY_FILAMENT_ID = "filament_id";
const BBL_JSON_KEY_FROM = "from";
const BBL_JSON_KEY_DESCRIPTION = "description";
const BBL_JSON_KEY_INSTANTIATION = "instantiation";
const BBL_JSON_KEY_IS_CUSTOM = "is_custom";
const BBL_JSON_KEY_INHERITS = "inherits";

// Type definitions
export type ConfigSubstitutions = ConfigSubstitution[];

export class ConfigSubstitutionContextImpl {
    constructor(
        public rule: ForwardCompatibilitySubstitutionRule,
        public substitutions: ConfigSubstitution[] = [],
        public unrecognizedKeys: string[] = []
    ) {}
}

// Helper functions
export function escapeStringCStyle(str: string): string {
    let result = '';
    for (let i = 0; i < str.length; i++) {
        const c = str[i];
        if (c === '\r') {
            result += '\\r';
        } else if (c === '\n') {
            result += '\\n';
        } else if (c === '\\' || c === '"') {
            result += '\\' + c;
        } else {
            result += c;
        }
    }
    return result;
}

export function unescapeStringCStyle(str: string): string {
    let result = '';
    let i = 0;
    while (i < str.length) {
        if (str[i] === '\\') {
            if (i === str.length - 1) {
                throw new Error('Invalid escape sequence at end of string');
            }
            i++;
            switch (str[i]) {
                case 'r':
                    result += '\r';
                    break;
                case 'n':
                    result += '\n';
                    break;
                case '\\':
                    result += '\\';
                    break;
                case '"':
                    result += '"';
                    break;
                default:
                    throw new Error(`Invalid escape sequence: \\${str[i]}`);
            }
        } else {
            result += str[i];
        }
        i++;
    }
    return result;
}

// Base Config class
export abstract class ConfigBase {
    protected options: Map<string, ConfigOption> = new Map();

    abstract def(): ConfigDef | null;

    // Get a config option by its key
    option(optKey: string, create: boolean = false): ConfigOption | null {
        const opt = this.options.get(optKey);
        if (opt) {
            return opt;
        }
        if (!create) {
            return null;
        }

        const def = this.def();
        if (!def) {
            throw new Error(`No definition for option ${optKey}`);
        }

        const optDef = def.get(optKey);
        if (!optDef) {
            return null;
        }

        const newOpt = optDef.createDefaultOption();
        this.options.set(optKey, newOpt);
        return newOpt;
    }

    // Set config value from string
    setDeserialize(optKey: string, value: string, substitutionsCtx: ConfigSubstitutionContextImpl, append: boolean = false): void {
        if (!this.setDeserializeNothrow(optKey, value, substitutionsCtx, append)) {
            throw new Error(`Invalid value for option ${optKey}: ${value}`);
        }
    }

    // Try to set config value from string, return success
    setDeserializeNothrow(optKey: string, value: string, substitutionsCtx: ConfigSubstitutionContextImpl, append: boolean = false): boolean {
        // Handle legacy options
        const [newKey, newValue] = this.handleLegacy(optKey, value);
        if (!newKey) {
            // Option was removed in newer version
            if (!substitutionsCtx.unrecognizedKeys.includes(optKey)) {
                substitutionsCtx.unrecognizedKeys.push(optKey);
            }
            return true;
        }

        return this.setDeserializeRaw(newKey, newValue, substitutionsCtx, append);
    }

    // Set raw config value from string
    protected setDeserializeRaw(optKey: string, value: string, substitutionsCtx: ConfigSubstitutionContextImpl, append: boolean): boolean {
        const def = this.def();
        if (!def) {
            throw new Error(`No definition for option ${optKey}`);
        }

        let foundOptDef = def.get(optKey);
        if (!foundOptDef) {
            for (const [key, opt] of def.options) {
                if (opt.aliases.includes(optKey)) {
                    optKey = key;
                    foundOptDef = opt;
                    break;
                }
            }
            if (!foundOptDef) {
                throw new Error(`Unknown option ${optKey}`);
            }
        }

        const optDef = foundOptDef;

        if (optDef.shortcut) {
            // Handle aliased options
            for (const shortcut of optDef.shortcut) {
                if (!this.setDeserializeRaw(shortcut, value, substitutionsCtx, append)) {
                    return false;
                }
            }
            return true;
        }

        const opt = this.option(optKey, true);
        if (!opt) {
            return false;
        }

        let success = false;
        let substituted = false;

        if (optDef.type === ConfigOptionType.Bools &&
            substitutionsCtx.rule !== ForwardCompatibilitySubstitutionRule.Disable) {
            // Special handling for boolean arrays
            const nullable = opt.nullable();
            const vectorOpt = opt as VectorConfigOption;
            const defaultValue = vectorOpt.values?.[0] === 1 ?
                DeserializationSubstitution.DefaultsToTrue :
                DeserializationSubstitution.DefaultsToFalse;

            const result = nullable ?
                (opt as ConfigOptionBoolsNullable).deserializeWithSubstitutions(value, append, defaultValue) :
                (opt as ConfigOptionBools).deserializeWithSubstitutions(value, append, defaultValue);

            success = result !== DeserializationResult.Failed;
            substituted = result === DeserializationResult.Substituted;

        } else {
            success = opt.deserialize(value, append);

            if (!success &&
                substitutionsCtx.rule !== ForwardCompatibilitySubstitutionRule.Disable &&
                (optDef.type === ConfigOptionType.Enum ||
                 optDef.type === ConfigOptionType.Enums ||
                 optDef.type === ConfigOptionType.Bool)) {

                // Try substitution for enums and bools
                if (optDef.type === ConfigOptionType.Bool) {
                    (opt as ConfigOptionBool).value = ConfigHelpers.enumLooksLikeTrueValue(value);
                } else if (optDef.default_value) {
                    opt.set(optDef.default_value);
                }
                success = true;
                substituted = true;
            }
        }

        if (substituted &&
            (substitutionsCtx.rule === ForwardCompatibilitySubstitutionRule.Enable ||
             substitutionsCtx.rule === ForwardCompatibilitySubstitutionRule.EnableSystemSilent)) {

            // Log substitution
            const configSubstitution: ConfigSubstitution = {
                optDef: optDef,
                oldValue: value,
                newValue: opt.clone()
            };
            substitutionsCtx.substitutions.push(configSubstitution);
        }

        return success;
    }

    // Get absolute value for a config option
    getAbsValue(optKey: string): number {
        const rawOpt = this.option(optKey);
        if (!rawOpt) {
            throw new Error(`Option ${optKey} not found`);
        }

        if (rawOpt.type === ConfigOptionType.Float) {
            return (rawOpt as ConfigOptionFloat).value;
        }

        if (rawOpt.type === ConfigOptionType.Int) {
            return (rawOpt as ConfigOptionInt).value;
        }

        if (rawOpt.type === ConfigOptionType.Bool) {
            return (rawOpt as ConfigOptionBool).value ? 1 : 0;
        }

        let castOpt: ConfigOptionPercent | null = null;

        if (rawOpt.type === ConfigOptionType.FloatOrPercent) {
            const cofop = rawOpt as ConfigOptionFloatOrPercent;
            if (cofop.value === 0 && optKey.endsWith('_line_width')) {
                return this.getAbsValue('line_width');
            }
            if (!cofop.percent) {
                return cofop.value;
            }
            castOpt = cofop;
        }

        if (rawOpt.type === ConfigOptionType.Percent) {
            castOpt = rawOpt as ConfigOptionPercent;
        }

        const def = this.def();
        if (!def) {
            throw new Error(`No definition for option ${optKey}`);
        }

        const optDef = def.get(optKey);
        if (!optDef) {
            throw new Error(`Option definition not found for ${optKey}`);
        }

        if (!optDef.ratio_over) {
            return castOpt!.getAbsValue(1);
        }

        return optDef.ratio_over === '' ? 0 :
            (rawOpt as ConfigOptionFloatOrPercent).getAbsValue(
                this.getAbsValue(optDef.ratio_over)
            );
    }

    // Get absolute value relative to another value
    getAbsValueRatio(optKey: string, ratioOver: number): number {
        const rawOpt = this.option(optKey);
        if (!rawOpt) {
            throw new Error(`Option ${optKey} not found`);
        }

        if (rawOpt.type !== ConfigOptionType.FloatOrPercent) {
            throw new Error(`Option ${optKey} is not FloatOrPercent`);
        }

        return (rawOpt as ConfigOptionFloatOrPercent).getAbsValue(ratioOver);
    }

    // Set environment variables from config
    setEnv(): void {
        const optKeys = this.keys();
        for (const key of optKeys) {
            const envName = 'SLIC3R_' + key.toUpperCase();
            process.env[envName] = this.optSerialize(key);
        }
    }

    // Load config from string map
    loadStringMap(keyValues: Map<string, string>, compatibilityRule: ForwardCompatibilitySubstitutionRule): ConfigSubstitutions {
        const substitutionsCtx = new ConfigSubstitutionContextImpl(compatibilityRule);

        for (const [key, value] of keyValues) {
            try {
                this.setDeserialize(key, value, substitutionsCtx);
            } catch (e) {
                if (e instanceof UnknownOptionException) {
                    // Ignore unknown options
                    continue;
                }
                throw e;
            }
        }

        return substitutionsCtx.substitutions;
    }

    // Load config from file
    load(file: string, compatibilityRule: ForwardCompatibilitySubstitutionRule): ConfigSubstitutions {
        if (Utils.isGCodeFile(file)) {
            throw new Error('GCode file loading not implemented');
        }

        if (Utils.isJsonFile(file)) {
            const keyValues = new Map<string, string>();
            const reason = { value: '' };
            return this.loadFromJson(file, compatibilityRule, keyValues, reason);
        }

        console.error('Unsupported format for config file', file);
        return [];
    }

    // Load from JSON file
    loadFromJson(
        file: string,
        compatibilityRule: ForwardCompatibilitySubstitutionRule,
        keyValues: Map<string, string>,
        reason: { value: string }
    ): ConfigSubstitutions {
        const substitutionsCtx = new ConfigSubstitutionContextImpl(compatibilityRule);

        try {
            const result = this.loadFromJsonInternal(
                file,
                substitutionsCtx,
                true,
                keyValues,
                reason
            );

            if (result !== 0) {
                throw new Error(reason.value);
            }

        } catch (e) {
            console.error('Error loading JSON:', e);
            if (e instanceof Error) {
                reason.value = `Error: ${e.message}`;
            } else {
                reason.value = 'Unknown error';
            }
            return [];
        }

        return substitutionsCtx.substitutions;
    }

    protected loadFromJsonInternal(
        file: string,
        substitutionContext: ConfigSubstitutionContextImpl,
        loadInheritsToConfig: boolean,
        keyValues: Map<string, string>,
        reason: { value: string }
    ): number {
        try {
            const data = fs.readFileSync(file, 'utf8');
            const j = JSON.parse(data) as JsonType;

            const differentSettingsAppend: string[] = [];
            let newSupportStyle = '';
            let isInfillFirst = '';
            let getWallSequence = '';
            let isProjectSettings = false;

            const configDef = this.def();
            if (!configDef) {
                console.error('No config definitions!');
                return -1;
            }

            // Parse JSON elements
            for (const [key, value] of Object.entries(j)) {
                if (key.toLowerCase() === BBL_JSON_KEY_VERSION.toLowerCase()) {
                    keyValues.set(BBL_JSON_KEY_VERSION, String(value));
                }
                else if (key.toLowerCase() === BBL_JSON_KEY_IS_CUSTOM.toLowerCase()) {
                    keyValues.set(BBL_JSON_KEY_IS_CUSTOM, String(value));
                }
                else if (key.toLowerCase() === BBL_JSON_KEY_NAME.toLowerCase()) {
                    keyValues.set(BBL_JSON_KEY_NAME, String(value));
                    if (value === 'project_settings') {
                        isProjectSettings = true;
                    }
                }
                // ... Handle other special keys similarly

                else {
                    const optKey = key;

                    if (typeof value === 'string') {
                        this.setDeserialize(optKey, value, substitutionContext);

                        // Handle special values
                        if (optKey === 'support_type' && value === 'hybrid(auto)') {
                            differentSettingsAppend.push(optKey);
                            differentSettingsAppend.push('support_style');
                            newSupportStyle = 'tree_hybrid';
                        }
                        // ... Handle other special cases
                    }
                    else if (Array.isArray(value)) {
                        // Handle array values
                        const valueStr = value
                            .map(v => typeof v === 'string' ?
                                `"${escapeStringCStyle(v)}"` :
                                String(v))
                            .join(',');

                        this.setDeserialize(optKey, valueStr, substitutionContext);
                    }
                }
            }

            // Handle different settings append
            if (differentSettingsAppend.length > 0) {
                if (newSupportStyle) {
                    const opt = this.option('support_style', true);
                    if (opt && opt.type === ConfigOptionType.Enum) {
                        opt.deserialize('tree_hybrid');
                    }
                }

                if (isInfillFirst) {
                    const opt = this.option('is_infill_first', true);
                    if (opt && opt.type === ConfigOptionType.Bool) {
                        opt.deserialize('true');
                    }
                }

                if (isProjectSettings) {
                    // Update different settings
                    const opt = this.option('different_settings_to_system', true);
                    if (opt && opt.type === ConfigOptionType.Strings) {
                        const differentSettings = (opt as VectorConfigOption).values;
                        // ... Handle project settings updates
                    }
                }
            }

            // Handle legacy conversions
            this.handleLegacyComposite();

            return 0;

        } catch (e) {
            console.error('Error parsing JSON file:', e);
            if (e instanceof Error) {
                reason.value = `Error: ${e.message}`;
            } else {
                reason.value = 'Unknown error';
            }
            return -1;
        }
    }

    // Abstract methods to implement
    abstract keys(): string[];
    abstract handleLegacy(key: string, value: string): [string, string];
    abstract handleLegacyComposite(): void;
    abstract optSerialize(key: string): string;
}

// Dynamic config implementation
export class DynamicConfig extends ConfigBase {
    constructor(rhs?: ConfigBase, keys?: string[]) {
        super();
        if (rhs && keys) {
            for (const key of keys) {
                const opt = rhs.option(key);
                if (opt) {
                    this.options.set(key, opt.clone());
                }
            }
        }
    }

    def(): ConfigDef | null {
        return null; // Dynamic config has no fixed definition
    }

    keys(): string[] {
        return Array.from(this.options.keys());
    }

    apply(other: DynamicConfig, create: boolean = false): void {
        for (const [key, value] of other.options) {
            const opt = this.option(key, create);
            if (opt) {
                opt.set(value);
            }
        }
    }

    applyOnly(other: DynamicConfig, keys: string[]): void {
        for (const key of keys) {
            const otherOpt = other.option(key);
            const thisOpt = this.option(key, true);
            if (otherOpt && thisOpt) {
                thisOpt.set(otherOpt);
            }
        }
    }

    handleLegacy(key: string, value: string): [string, string] {
        // No legacy handling in dynamic config
        return [key, value];
    }

    handleLegacyComposite(): void {
        // No composite legacy handling in dynamic config
    }

    optSerialize(key: string): string {
        const opt = this.option(key);
        if (!opt) throw new Error(`Option ${key} not found`);
        return opt.serialize();
    }
}

// Static config implementation
export class StaticConfig extends ConfigBase {
    private definition: ConfigDef;

    constructor(definition: ConfigDef) {
        super();
        this.definition = definition;
    }

    def(): ConfigDef {
        return this.definition;
    }

    setDefaults(): void {
        const defs = this.def();
        if (defs) {
            for (const key of this.keys()) {
                const def = defs.get(key);
                const opt = this.option(key);
                if (def && opt && def.default_value) {
                    opt.set(def.default_value);
                }
            }
        }
    }

    keys(): string[] {
        return Array.from(this.definition.options.keys());
    }

    option(optKey: string, create: boolean = false): ConfigOption | null {
        const opt = this.options.get(optKey);
        if (opt) {
            return opt;
        }
        if (!create) {
            return null;
        }

        const def = this.def();
        if (!def) {
            throw new Error(`No definition for option ${optKey}`);
        }

        const optDef = def.get(optKey);
        if (!optDef) {
            throw new UnknownOptionException(optKey);
        }

        const newOpt = optDef.createDefaultOption();
        this.options.set(optKey, newOpt);
        return newOpt;
    }

    handleLegacy(key: string, value: string): [string, string] {
        // No legacy handling in static config
        return [key, value];
    }

    handleLegacyComposite(): void {
        // No composite legacy handling in static config
    }

    optSerialize(key: string): string {
        const opt = this.option(key);
        if (!opt) throw new Error(`Option ${key} not found`);
        return opt.serialize();
    }
}

// Type declarations for external modules
export interface JsonType {
    [key: string]: any;
}

export interface BoostType {
    property_tree: {
        ptree: any;
    };
}