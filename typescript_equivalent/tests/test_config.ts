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
    unescapeStringCStyle
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

// Add more test cases for other config options and functionality...