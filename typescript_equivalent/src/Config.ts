// TypeScript equivalent of Config.cpp
import { format, floatToStringDecimalPoint } from './format';
import { Utils } from './Utils';
import { LocalesUtils, CNumericLocalesSetter } from './LocalesUtils';
import {
    ConfigOption,
    ConfigOptionType,
    ConfigDef,
    ConfigOptionDef,
    ConfigSubstitution,
    ForwardCompatibilitySubstitutionRule,
    DeserializationResult,
    DeserializationSubstitution,
    SupportMaterialStyle,
    UnknownOptionException,
    ConfigOptionFloat,
    ConfigOptionInt,
    ConfigOptionBool,
    ConfigOptionBools,
    ConfigOptionBoolsNullable,
    ConfigOptionPercent,
    ConfigOptionFloatOrPercent,
    ConfigHelpers,
    VectorConfigOption,
    JsonType,
    BoostType
} from './types';

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
        const keys: string[] = [];
        const defs = this.def();
        if (defs) {
            for (const [key, optDef] of defs.options) {
                if (this.option(key)) {
                    keys.push(key);
                }
            }
        }
        return keys;
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