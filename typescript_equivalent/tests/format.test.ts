import { describe, test, expect } from '@jest/globals';
import { Slic3r } from '../src/format';

// Custom type to test format with user-defined types
class CustomType {
    constructor(private value: number) {}
    toString(): string {
        return `CustomType(${this.value})`;
    }
}

// Helper function to convert vector to string (similar to C++ version)
function vecToString<T>(vec: T[]): string {
    return `[${vec.join(', ')}]`;
}

describe('Basic Format Tests', () => {
    test('Empty format string', () => {
        expect(Slic3r.format('')).toBe('');
        expect(Slic3r.format(String(''))).toBe('');
    });

    test('String without placeholders', () => {
        expect(Slic3r.format('Hello World')).toBe('Hello World');
        expect(Slic3r.format(String('Hello World'))).toBe('Hello World');
    });

    test('Single argument formatting', () => {
        expect(Slic3r.format('Number: %s', 42)).toBe('Number: 42');
        expect(Slic3r.format('String: %s', 'test')).toBe('String: test');
        expect(parseFloat(Slic3r.format('%s', 3.14159))).toBeCloseTo(3.14159, 5);
        expect(Slic3r.format('Bool: %s', true)).toBe('Bool: true');
    });

    test('Multiple argument formatting', () => {
        expect(Slic3r.format('%s + %s = %s', 1, 2, 3)).toBe('1 + 2 = 3');
        expect(Slic3r.format('%s, %s, %s', 'a', 'b', 'c')).toBe('a, b, c');
    });
});

describe('Numeric Format Tests', () => {
    test('Integer types', () => {
        expect(Slic3r.format('%s', 42)).toBe('42');
        expect(Slic3r.format('%s', BigInt(42))).toBe('42');
    });

    test('Floating point types', () => {
        expect(parseFloat(Slic3r.format('%s', 3.14))).toBeCloseTo(3.14, 2);
        expect(parseFloat(Slic3r.format('%s', 3.14159))).toBeCloseTo(3.14159, 5);
    });

    test('Special numeric values', () => {
        expect(Slic3r.format('%s', Infinity)).toBe('Infinity');
        expect(Slic3r.format('%s', -Infinity)).toBe('-Infinity');
        expect(Slic3r.format('%s', NaN)).toBe('NaN');
    });
});

describe('String Format Tests', () => {
    test('String literals and types', () => {
        expect(Slic3r.format('%s', 'literal')).toBe('literal');
        expect(Slic3r.format('%s', String('string'))).toBe('string');
    });

    test('Empty strings', () => {
        expect(Slic3r.format('%s', '')).toBe('');
        expect(Slic3r.format('%s', String())).toBe('');
    });

    test('Strings with special characters', () => {
        expect(Slic3r.format('%s', 'Hello\nWorld')).toBe('Hello\nWorld');
        expect(Slic3r.format('%s', 'Tab\there')).toBe('Tab\there');
        expect(Slic3r.format('%s', String.raw`Raw\string`)).toBe(String.raw`Raw\string`);
    });
});

describe('Custom Type Format Tests', () => {
    test('Custom type with toString', () => {
        const ct = new CustomType(42);
        expect(Slic3r.format('%s', ct)).toBe('CustomType(42)');
    });
});

describe('Container Format Tests', () => {
    test('Array formatting', () => {
        const arr = [1, 2, 3];
        expect(Slic3r.format('%s', vecToString(arr))).toBe('[1, 2, 3]');
    });

    test('Empty container formatting', () => {
        const emptyArr: number[] = [];
        expect(Slic3r.format('%s', vecToString(emptyArr))).toBe('[]');
    });
});

describe('Error Handling Tests', () => {
    test('Too few arguments', () => {
        expect(() => Slic3r.format('%s %s', 1)).toThrow();
    });

    test('Invalid format string', () => {
        expect(() => Slic3r.format('%')).toThrow();
    });
});

describe('Mixed Type Format Tests', () => {
    test('Mixed numeric and string', () => {
        expect(Slic3r.format('%s %s %s', 42, 'test', 3.14))
            .toBe('42 test 3.14');
    });

    test('Mixed custom and standard types', () => {
        const ct = new CustomType(42);
        expect(Slic3r.format('%s %s %s', ct, 'test', 3.14))
            .toBe('CustomType(42) test 3.14');
    });
});

describe('Thread Safety Tests', () => {
    test('Concurrent formatting', async () => {
        const numThreads = 4;
        const promises = Array.from({ length: numThreads }, (_, i) =>
            Promise.resolve(Slic3r.format('Thread %s', i))
        );

        const results = await Promise.all(promises);

        results.forEach((result, i) => {
            expect(result).toBe(Slic3r.format('Thread %s', i));
        });
    });
});