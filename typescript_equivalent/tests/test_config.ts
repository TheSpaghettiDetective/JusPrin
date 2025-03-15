import { expect } from 'chai';
import { describe, it, beforeEach } from 'mocha';
import {
    ConfigOptionFloat,
    ConfigOptionInt,
    ConfigOptionString,
    ConfigOptionBool,
    ConfigOptionBools,
    ConfigOptionBoolsNullable,
    ConfigOptionPercent,
    ConfigOptionFloatOrPercent,
    ConfigOptionPoint,
    ConfigOptionPoint3,
    ConfigOptionType,
    ConfigOptionDef,
    DeserializationResult,
    DeserializationSubstitution,
    SupportMaterialStyle,
    UnknownOptionException,
    ConfigHelpers,
    ConfigOptionStrings,
    ConfigOption,
    DynamicConfig,
    escapeStringCStyle,
    unescapeStringCStyle,
    StaticConfig
} from '../src/Config';
import { Vec2d, Vec3d } from '../src/geometry';
import { createVec2d, createVec3d } from './test_helpers';

// Helper function to test serialization/deserialization
function testSerializeDeserialize<T extends { serialize(): string; deserialize(str: string): boolean }>(
    original: T,
    expectedStr: string
) {
    expect(original.serialize()).to.equal(expectedStr);
    const deserialized = new (original.constructor as new () => T)();
    expect(deserialized.deserialize(expectedStr)).to.be.true;
    expect(deserialized.serialize()).to.equal(expectedStr);
}

describe('ConfigOption Basic Tests', () => {
    it('ConfigOptionFloat', () => {
        const opt = new ConfigOptionFloat();
        opt.value = 1.5;
        expect(opt.serialize()).to.equal('1.5');

        const opt2 = new ConfigOptionFloat();
        expect(opt2.deserialize('2.5')).to.be.true;
        expect(opt2.value).to.equal(2.5);
    });

    it('ConfigOptionInt', () => {
        const opt = new ConfigOptionInt();
        opt.value = 42;
        expect(opt.serialize()).to.equal('42');

        const opt2 = new ConfigOptionInt();
        expect(opt2.deserialize('24')).to.be.true;
        expect(opt2.value).to.equal(24);
    });

    it('ConfigOptionString', () => {
        const opt = new ConfigOptionString();
        opt.value = 'test';
        expect(opt.serialize()).to.equal('test');

        const opt2 = new ConfigOptionString();
        expect(opt2.deserialize('value')).to.be.true;
        expect(opt2.value).to.equal('value');
    });

    it('ConfigOptionBool', () => {
        const opt = new ConfigOptionBool();
        opt.value = true;
        expect(opt.serialize()).to.equal('true');

        const opt2 = new ConfigOptionBool();
        expect(opt2.deserialize('1')).to.be.true;
        expect(opt2.value).to.be.true;
        expect(opt2.deserialize('0')).to.be.true;
        expect(opt2.value).to.be.false;
    });

    it('DynamicConfig apply', () => {
        const config = new DynamicConfig();
        const other = new DynamicConfig();
        const keys: string[] = [];

        config.apply(other, true);
        config.apply(other);
        config.applyOnly(other, keys);
    });
});

describe('ConfigOption Def Tests', () => {
    let def: {
        type: ConfigOptionType;
        default_value?: ConfigOption;
        aliases: string[];
        shortcut: string[];
        cli: string;
        ratio_over: string;
        nullable: boolean;
        createDefaultOption: () => ConfigOption;
        createEmptyOption: () => ConfigOption;
    };

    beforeEach(() => {
        def = {
            type: ConfigOptionType.Float,
            default_value: undefined,
            aliases: [],
            shortcut: [],
            cli: '',
            ratio_over: '',
            nullable: false,
            createDefaultOption: () => new ConfigOptionFloat(),
            createEmptyOption: () => new ConfigOptionFloat()
        };
    });

    it('create_empty_option', () => {
        def.type = ConfigOptionType.Float;
        def.createEmptyOption = () => new ConfigOptionFloat();
        def.createEmptyOption();

        def.type = ConfigOptionType.Int;
        def.createEmptyOption = () => new ConfigOptionInt();
        def.createEmptyOption();

        def.type = ConfigOptionType.String;
        def.createEmptyOption = () => new ConfigOptionString();
        def.createEmptyOption();

        def.type = ConfigOptionType.Bool;
        def.createEmptyOption = () => new ConfigOptionBool();
        def.createEmptyOption();
    });

    it('create_default_option', () => {
        def.type = ConfigOptionType.Float;
        def.createDefaultOption = () => new ConfigOptionFloat();
        def.createDefaultOption();

        def.type = ConfigOptionType.Int;
        def.createDefaultOption = () => new ConfigOptionInt();
        def.createDefaultOption();

        def.type = ConfigOptionType.Bool;
        def.createDefaultOption = () => new ConfigOptionBool();
        def.createDefaultOption();
    });

    it('cli_args', () => {
        def.cli = '';
        expect(def.cli).to.equal('');

        def.cli = 'custom-arg';
        expect(def.cli).to.equal('custom-arg');
    });
});

describe('ConfigOption Error Handling', () => {
    it('Invalid Float', () => {
        const opt = new ConfigOptionFloat();
        expect(opt.deserialize('not_a_number')).to.be.false;
    });

    it('Invalid Int', () => {
        const opt = new ConfigOptionInt();
        expect(opt.deserialize('not_a_number')).to.be.false;
    });

    it('Invalid Bool', () => {
        const opt = new ConfigOptionBool();
        expect(opt.deserialize('not_a_bool')).to.be.false;
    });
});

describe('ConfigOption Edge Cases', () => {
    it('Float Zero', () => {
        const opt = new ConfigOptionFloat();
        opt.value = 0.0;
        expect(opt.serialize()).to.equal('0');
    });

    it('Float Negative', () => {
        const opt = new ConfigOptionFloat();
        opt.value = -1.5;
        expect(opt.serialize()).to.equal('-1.5');
    });

    it('Int Zero', () => {
        const opt = new ConfigOptionInt();
        opt.value = 0;
        expect(opt.serialize()).to.equal('0');
    });

    it('Int Negative', () => {
        const opt = new ConfigOptionInt();
        opt.value = -42;
        expect(opt.serialize()).to.equal('-42');
    });

    it('Empty String', () => {
        const opt = new ConfigOptionString();
        opt.value = '';
        expect(opt.serialize()).to.equal('');
    });

    it('Clone and Equality', () => {
        const opt1 = new ConfigOptionFloat();
        opt1.value = 1.5;
        const opt2 = opt1.clone();
        expect(opt2).to.deep.equal(opt1);

        const opt3 = new ConfigOptionInt();
        opt3.value = 42;
        const opt4 = opt3.clone();
        expect(opt4).to.deep.equal(opt3);

        const opt5 = new ConfigOptionString();
        opt5.value = 'test';
        const opt6 = opt5.clone();
        expect(opt6).to.deep.equal(opt5);

        const opt7 = new ConfigOptionBool();
        opt7.value = true;
        const opt8 = opt7.clone();
        expect(opt8).to.deep.equal(opt7);
    });
});

describe('String escaping and unescaping', () => {
    it('String escaping', () => {
        const testCases = [
            ['simple', 'simple'],
            ['with spaces', 'with spaces'],
            ['with"quote', 'with\\"quote'],
            ['with\\backslash', 'with\\\\backslash'],
            ['with\nnewline', 'with\\nnewline'],
            ['with\rreturn', 'with\\rreturn']
        ];

        for (const [input, expected] of testCases) {
            expect(escapeStringCStyle(input)).to.equal(expected);
        }
    });

    it('String unescaping', () => {
        const testCases = [
            ['simple', 'simple'],
            ['with spaces', 'with spaces'],
            ['with\\"quote', 'with"quote'],
            ['with\\\\backslash', 'with\\backslash'],
            ['with\\nnewline', 'with\nnewline'],
            ['with\\rreturn', 'with\rreturn']
        ];

        for (const [input, expected] of testCases) {
            const result = unescapeStringCStyle(input);
            expect(result).to.equal(expected);
        }

        // Invalid escape sequence
        expect(() => unescapeStringCStyle('invalid\\')).to.throw();
    });
});

describe('FloatOrPercent', () => {
    it('should handle numeric values', () => {
        const opt = new ConfigOptionFloatOrPercent();
        opt.value = 1.5;
        opt.percent = false;
        expect(opt.value).to.equal(1.5);
        expect(opt.percent).to.be.false;
    });

    it('should handle percentage values', () => {
        const opt = new ConfigOptionFloatOrPercent();
        opt.value = 0.5;
        opt.percent = true;
        expect(opt.value).to.equal(0.5);
        expect(opt.percent).to.be.true;
    });

    it('should calculate absolute values correctly', () => {
        const numericOpt = new ConfigOptionFloatOrPercent();
        numericOpt.value = 1.5;
        numericOpt.percent = false;

        const percentOpt = new ConfigOptionFloatOrPercent();
        percentOpt.value = 0.5;
        percentOpt.percent = true;

        expect(numericOpt.getAbsValue(100)).to.equal(1.5);
        expect(percentOpt.getAbsValue(100)).to.equal(50);
    });

    it('should serialize correctly', () => {
        const numericOpt = new ConfigOptionFloatOrPercent();
        numericOpt.value = 1.5;
        numericOpt.percent = false;

        const percentOpt = new ConfigOptionFloatOrPercent();
        percentOpt.value = 0.5;
        percentOpt.percent = true;

        expect(numericOpt.serialize()).to.equal('1.5');
        expect(percentOpt.serialize()).to.equal('50%');
    });

    it('should deserialize correctly', () => {
        const opt = new ConfigOptionFloatOrPercent();

        expect(opt.deserialize('1.5')).to.be.true;
        expect(opt.value).to.equal(1.5);
        expect(opt.percent).to.be.false;

        expect(opt.deserialize('50%')).to.be.true;
        expect(opt.value).to.equal(0.5);
        expect(opt.percent).to.be.true;
    });

    it('should handle invalid deserialization', () => {
        const opt = new ConfigOptionFloatOrPercent();
        expect(opt.deserialize('invalid')).to.be.false;
    });
});

describe('Error Handling', () => {
    describe('UnknownOptionException', () => {
        it('should throw with correct message', () => {
            const err = new UnknownOptionException('test_key');
            expect(() => { throw err; })
                .to.throw('Unknown option: test_key');
        });
    });

    describe('Type Compatibility', () => {
        it('should throw when comparing incompatible types', () => {
            const floatOpt = new ConfigOptionFloat();
            floatOpt.value = 1.5;
            const intOpt = new ConfigOptionInt();
            intOpt.value = 1;

            expect(() => floatOpt.set(intOpt))
                .to.throw('ConfigOptionSingle: Assigning an incompatible type');
        });

        it('should throw when setting incompatible types', () => {
            const floatOpt = new ConfigOptionFloat();
            floatOpt.value = 1.5;
            const boolOpt = new ConfigOptionBool();
            boolOpt.value = true;

            expect(() => floatOpt.set(boolOpt))
                .to.throw('ConfigOptionSingle: Assigning an incompatible type');
        });
    });
});

describe('ConfigOptionVector Base Functionality', () => {
    describe('ConfigOptionBools', () => {
        it('should handle vector operations', () => {
            const opt = new ConfigOptionBools();

            // Test empty vector
            expect(opt.values).to.be.empty;

            // Test adding values
            opt.values.push(true);
            opt.values.push(false);
            expect(opt.values).to.have.lengthOf(2);
            expect(opt.values[0]).to.be.true;
            expect(opt.values[1]).to.be.false;

            // Test serialization
            expect(opt.serialize()).to.equal('true,false');

            // Test deserialization
            const opt2 = new ConfigOptionBools();
            expect(opt2.deserialize('true')).to.be.true;
            expect(opt2.values).to.have.lengthOf(1);
            expect(opt2.values[0]).to.be.true;

            // Test append
            expect(opt2.deserialize('false', true)).to.be.true;
            expect(opt2.values).to.have.lengthOf(2);
            expect(opt2.values[1]).to.be.false;
        });

        it('should handle cloning', () => {
            const opt = new ConfigOptionBools();
            opt.values = [true, false];

            const clone = opt.clone() as ConfigOptionBools;
            expect(clone.values).to.deep.equal(opt.values);
            expect(clone.values).to.not.equal(opt.values); // Different array instance
        });

        it('should handle setting from another option', () => {
            const opt1 = new ConfigOptionBools();
            opt1.values = [true, false];

            const opt2 = new ConfigOptionBools();
            opt2.set(opt1);
            expect(opt2.values).to.deep.equal(opt1.values);
        });
    });
});

describe('ConfigOptionBoolsNullable', () => {
    it('should handle nullable values', () => {
        const opt = new ConfigOptionBoolsNullable();
        expect(opt.nullable()).to.be.true;

        // Test nil value handling
        expect(opt.deserialize('nil')).to.be.true;
        expect(opt.values).to.have.lengthOf(1);

        // Test regular value after nil
        expect(opt.deserialize('true', true)).to.be.true;
        expect(opt.values).to.have.lengthOf(2);
    });

    it('should handle substitutions', () => {
        const opt = new ConfigOptionBoolsNullable();

        // Test nil with DefaultsToTrue
        const result1 = opt.deserializeWithSubstitutions('nil', false, DeserializationSubstitution.DefaultsToTrue);
        expect(result1).to.equal(DeserializationResult.Substituted);
        expect(opt.values[0]).to.be.true;

        // Test nil with DefaultsToFalse
        opt.values = [];
        const result2 = opt.deserializeWithSubstitutions('nil', false, DeserializationSubstitution.DefaultsToFalse);
        expect(result2).to.equal(DeserializationResult.Substituted);
        expect(opt.values[0]).to.be.false;
    });
});

describe('ConfigOptionPoint Vector Operations', () => {
    it('should handle basic point operations', () => {
        const opt = new ConfigOptionPoint();

        // Test initial value
        expect(opt.value.x).to.equal(0);
        expect(opt.value.y).to.equal(0);

        // Test setting value
        opt.value = new Vec2d(1, 2);
        expect(opt.value.x).to.equal(1);
        expect(opt.value.y).to.equal(2);

        // Test serialization
        expect(opt.serialize()).to.equal('1,2');

        // Test deserialization
        const opt2 = new ConfigOptionPoint();
        expect(opt2.deserialize('3,4')).to.be.true;
        expect(opt2.value.x).to.equal(3);
        expect(opt2.value.y).to.equal(4);
    });

    it('should handle cloning', () => {
        const opt = new ConfigOptionPoint();
        opt.value = new Vec2d(1, 2);

        const clone = opt.clone() as ConfigOptionPoint;
        expect(clone.value.x).to.equal(opt.value.x);
        expect(clone.value.y).to.equal(opt.value.y);
        expect(clone.value).to.not.equal(opt.value); // Different Vec2d instance
    });

    it('should handle invalid deserialization', () => {
        const opt = new ConfigOptionPoint();
        expect(opt.deserialize('invalid')).to.be.false;
        expect(opt.deserialize('1')).to.be.false;
        expect(opt.deserialize('1,')).to.be.false;
        expect(opt.deserialize('1,a')).to.be.false;
    });
});

describe('ConfigOptionFloatOrPercent Vector Operations', () => {
    it('should handle basic operations', () => {
        const opt = new ConfigOptionFloatOrPercent();

        // Test initial values
        expect(opt.value).to.equal(0);
        expect(opt.percent).to.be.false;

        // Test setting values
        opt.value = 1.5;
        opt.percent = false;
        expect(opt.value).to.equal(1.5);
        expect(opt.percent).to.be.false;

        opt.value = 0.5;
        opt.percent = true;
        expect(opt.value).to.equal(0.5);
        expect(opt.percent).to.be.true;

        // Test serialization
        expect(opt.serialize()).to.equal('50%');

        // Test deserialization
        const opt2 = new ConfigOptionFloatOrPercent();
        expect(opt2.deserialize('2.5')).to.be.true;
        expect(opt2.value).to.equal(2.5);
        expect(opt2.percent).to.be.false;

        expect(opt2.deserialize('75%')).to.be.true;
        expect(opt2.value).to.equal(0.75);
        expect(opt2.percent).to.be.true;
    });

    it('should handle cloning', () => {
        const opt = new ConfigOptionFloatOrPercent();
        opt.value = 1.5;
        opt.percent = true;

        const clone = opt.clone() as ConfigOptionFloatOrPercent;
        expect(clone.value).to.equal(opt.value);
        expect(clone.percent).to.equal(opt.percent);
    });

    it('should handle invalid deserialization', () => {
        const opt = new ConfigOptionFloatOrPercent();
        expect(opt.deserialize('invalid')).to.be.false;
        expect(opt.deserialize('abc%')).to.be.false;
    });
});

describe('StaticConfig', () => {
    let config: StaticConfig;

    beforeEach(() => {
        const optionsMap = new Map<string, ConfigOptionDef>();
        optionsMap.set('float_option', {
            type: ConfigOptionType.Float,
            default_value: undefined,
            aliases: [],
            shortcut: [],
            createDefaultOption: () => new ConfigOptionFloat()
        });
        optionsMap.set('int_option', {
            type: ConfigOptionType.Int,
            default_value: undefined,
            aliases: [],
            shortcut: [],
            createDefaultOption: () => new ConfigOptionInt()
        });

        const definition = {
            options: optionsMap,
            get: (key: string) => optionsMap.get(key) || null,
            has: (key: string) => optionsMap.has(key),
            empty: () => optionsMap.size === 0,
            keys: () => Array.from(optionsMap.keys())
        };
        config = new StaticConfig(definition);
    });

    it('should handle getting options', () => {
        const floatOpt = config.option('float_option', true) as ConfigOptionFloat;
        expect(floatOpt).to.not.be.null;
        expect(floatOpt.type).to.equal(ConfigOptionType.Float);

        const intOpt = config.option('int_option', true) as ConfigOptionInt;
        expect(intOpt).to.not.be.null;
        expect(intOpt.type).to.equal(ConfigOptionType.Int);
    });

    it('should return keys correctly', () => {
        const keys = config.keys();
        expect(keys).to.have.lengthOf(2);
        expect(keys).to.include('float_option');
        expect(keys).to.include('int_option');
    });

    it('should handle unknown options', () => {
        expect(() => config.option('nonexistent', true))
            .to.throw('Unknown option: nonexistent');
    });
});