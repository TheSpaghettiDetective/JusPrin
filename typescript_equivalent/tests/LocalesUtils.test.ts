import { describe, test, expect, beforeEach, afterEach } from '@jest/globals';
import { Slic3r } from '../src/LocalesUtils';

describe('CNumericLocalesSetter Tests', () => {
    test('Basic Functionality', () => {
        // Store original locale
        const originalLocale = Intl.NumberFormat().resolvedOptions().locale;

        {
            const setter = new Slic3r.CNumericLocalesSetter();
            // Verify decimal point is now '.'
            expect(Slic3r.isDecimalSeparatorPoint()).toBe(true);

            // Test some float formatting
            expect(new Intl.NumberFormat('en-US').format(1.5)).toBe('1.5');

            setter.dispose();
        }

        // Verify locale is restored
        expect(Intl.NumberFormat().resolvedOptions().locale).toBe(originalLocale);
    });

    test('Thread Safety', async () => {
        const NUM_THREADS = 4;
        const promises = Array.from({ length: NUM_THREADS }, async () => {
            try {
                const setter = new Slic3r.CNumericLocalesSetter();
                // Perform some locale-dependent operations
                const result = Slic3r.isDecimalSeparatorPoint() &&
                    !isNaN(Slic3r.stringToDoubleDecimalPoint('123.456')) &&
                    Slic3r.floatToStringDecimalPoint(123.456) !== '';
                setter.dispose();
                return result;
            } catch {
                return false;
            }
        });

        const results = await Promise.all(promises);
        results.forEach(result => {
            expect(result).toBe(true);
        });
    });
});

describe('stringToDoubleDecimalPoint Tests', () => {
    let setter: Slic3r.CNumericLocalesSetter;

    beforeEach(() => {
        setter = new Slic3r.CNumericLocalesSetter();
    });

    afterEach(() => {
        setter.dispose();
    });

    test('Valid Conversions', () => {
        const pos = { value: 0 };

        // Test positive numbers
        expect(Slic3r.stringToDoubleDecimalPoint('123.456', pos)).toBeCloseTo(123.456);
        expect(pos.value).toBe(7);

        // Test negative numbers
        expect(Slic3r.stringToDoubleDecimalPoint('-123.456', pos)).toBeCloseTo(-123.456);
        expect(pos.value).toBe(8);

        // Test scientific notation
        expect(Slic3r.stringToDoubleDecimalPoint('1.23e-4', pos)).toBeCloseTo(0.000123);
        expect(Slic3r.stringToDoubleDecimalPoint('1.23E+4', pos)).toBeCloseTo(12300.0);

        // Test integers
        expect(Slic3r.stringToDoubleDecimalPoint('42', pos)).toBeCloseTo(42.0);

        // Test zero
        expect(Slic3r.stringToDoubleDecimalPoint('0.0', pos)).toBeCloseTo(0.0);
        expect(Slic3r.stringToDoubleDecimalPoint('-0.0', pos)).toBeCloseTo(0.0);
    });

    test('Edge Cases', () => {
        const pos = { value: 0 };

        // Test empty string
        expect(isNaN(Slic3r.stringToDoubleDecimalPoint(''))).toBe(true);
        expect(pos.value).toBe(0);

        // Test whitespace handling
        expect(Slic3r.stringToDoubleDecimalPoint('  123.456', pos)).toBeCloseTo(123.456);

        // Test very large and small numbers
        expect(Slic3r.stringToDoubleDecimalPoint('1e308', pos)).toBeCloseTo(1e308);
        expect(Slic3r.stringToDoubleDecimalPoint('1e-308', pos)).toBeCloseTo(1e-308);
    });
});

describe('floatToStringDecimalPoint Tests', () => {
    let setter: Slic3r.CNumericLocalesSetter;

    beforeEach(() => {
        setter = new Slic3r.CNumericLocalesSetter();
    });

    afterEach(() => {
        setter.dispose();
    });

    test('Default Precision', () => {
        expect(Slic3r.floatToStringDecimalPoint(123.456)).toBe('123.456');
        expect(Slic3r.floatToStringDecimalPoint(-123.456)).toBe('-123.456');
        expect(Slic3r.floatToStringDecimalPoint(0.0)).toBe('0');
        expect(Slic3r.floatToStringDecimalPoint(-0.0)).toBe('0');
    });

    test('Custom Precision', () => {
        expect(Slic3r.floatToStringDecimalPoint(123.456, 2)).toBe('123.46');
        expect(Slic3r.floatToStringDecimalPoint(123.456, 0)).toBe('123');
        expect(Slic3r.floatToStringDecimalPoint(123.456, 4)).toBe('123.4560');
    });

    test('Edge Cases', () => {
        // Very large numbers
        expect(() => Slic3r.floatToStringDecimalPoint(1e308)).not.toThrow();

        // Very small numbers
        expect(() => Slic3r.floatToStringDecimalPoint(1e-308)).not.toThrow();

        // Zero with different precisions
        expect(Slic3r.floatToStringDecimalPoint(0.0, 2)).toBe('0.00');
        expect(Slic3r.floatToStringDecimalPoint(-0.0, 2)).toBe('0.00');
    });
});

describe('isDecimalSeparatorPoint Tests', () => {
    test('Basic Functionality', () => {
        const setter = new Slic3r.CNumericLocalesSetter();
        expect(Slic3r.isDecimalSeparatorPoint()).toBe(true);
        setter.dispose();
    });

    test('Locale Change', () => {
        // Test with German locale (uses comma as decimal separator)
        const germanFormatter = new Intl.NumberFormat('de-DE');
        expect(germanFormatter.format(0.5).includes(',')).toBe(true);

        // Verify CNumericLocalesSetter fixes it
        const setter = new Slic3r.CNumericLocalesSetter();
        expect(Slic3r.isDecimalSeparatorPoint()).toBe(true);
        setter.dispose();
    });
});