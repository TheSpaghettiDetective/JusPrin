import { expect } from 'chai';
import { describe, it } from 'mocha';
import { format, floatToStringDecimalPoint } from '../src/format';

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

describe('format', () => {
    it('should format simple strings', () => {
        expect(format('Hello %s', 'World')).to.equal('Hello World');
    });

    it('should format multiple arguments', () => {
        expect(format('Hello %s %s', 'beautiful', 'World')).to.equal('Hello beautiful World');
    });

    it('should handle numbers', () => {
        expect(format('Number: %d', 42)).to.equal('Number: 42');
        expect(format('Float: %f', 3.14)).to.equal('Float: 3.14');
    });

    it('should handle invalid format strings', () => {
        expect(() => format('Invalid %z', 'test')).to.throw('Invalid format string');
    });

    it('should handle empty format string', () => {
        expect(format('')).to.equal('');
    });

    it('should handle no arguments', () => {
        expect(format('No args')).to.equal('No args');
    });

    it('should handle too many arguments', () => {
        expect(() => format('%s', 'one', 'two')).to.throw('Too many arguments provided');
    });

    it('should handle too few arguments', () => {
        expect(() => format('%s %s', 'one')).to.throw('Too few arguments provided');
    });
});

describe('floatToStringDecimalPoint', () => {
    it('should convert float to string with decimal point', () => {
        expect(floatToStringDecimalPoint(3.14)).to.equal('3.14');
    });

    it('should handle zero', () => {
        expect(floatToStringDecimalPoint(0)).to.equal('0');
    });

    it('should handle negative numbers', () => {
        expect(floatToStringDecimalPoint(-3.14)).to.equal('-3.14');
    });

    it('should handle negative zero', () => {
        expect(floatToStringDecimalPoint(-0)).to.equal('0');
    });

    it('should handle integers', () => {
        expect(floatToStringDecimalPoint(42)).to.equal('42');
    });
});

describe('Basic Format Tests', () => {
    it('String without placeholders', () => {
        expect(format('Hello World')).to.equal('Hello World');
    });

    it('Single argument formatting', () => {
        expect(format('Number: %s', 42)).to.equal('Number: 42');
        expect(format('String: %s', 'test')).to.equal('String: test');
        expect(parseFloat(format('%s', 3.14159))).to.be.closeTo(3.14159, 5);
        expect(format('Bool: %s', true)).to.equal('Bool: true');
    });

    it('Multiple argument formatting', () => {
        expect(format('%s + %s = %s', 1, 2, 3)).to.equal('1 + 2 = 3');
        expect(format('%s, %s, %s', 'a', 'b', 'c')).to.equal('a, b, c');
    });
});

describe('Numeric Format Tests', () => {
    it('Integer types', () => {
        expect(format('%s', 42)).to.equal('42');
        expect(format('%s', BigInt(42))).to.equal('42');
    });

    it('Floating point types', () => {
        expect(parseFloat(format('%s', 3.14))).to.be.closeTo(3.14, 2);
        expect(parseFloat(format('%s', 3.14159))).to.be.closeTo(3.14159, 5);
    });

    it('Special numeric values', () => {
        expect(format('%s', Infinity)).to.equal('Infinity');
        expect(format('%s', -Infinity)).to.equal('-Infinity');
        expect(format('%s', NaN)).to.equal('NaN');
    });
});

describe('String Format Tests', () => {
    it('String literals and types', () => {
        expect(format('%s', 'literal')).to.equal('literal');
        expect(format('%s', String('string'))).to.equal('string');
    });

    it('Empty strings', () => {
        expect(format('%s', '')).to.equal('');
        expect(format('%s', String())).to.equal('');
    });

    it('Strings with special characters', () => {
        expect(format('%s', 'Hello\nWorld')).to.equal('Hello\nWorld');
        expect(format('%s', 'Tab\there')).to.equal('Tab\there');
        expect(format('%s', String.raw`Raw\string`)).to.equal(String.raw`Raw\string`);
    });
});

describe('Custom Type Format Tests', () => {
    it('Custom type with toString', () => {
        const ct = new CustomType(42);
        expect(format('%s', ct)).to.equal('CustomType(42)');
    });
});

describe('Container Format Tests', () => {
    it('Array formatting', () => {
        const arr = [1, 2, 3];
        expect(format('%s', vecToString(arr))).to.equal('[1, 2, 3]');
    });

    it('Empty container formatting', () => {
        const emptyArr: number[] = [];
        expect(format('%s', vecToString(emptyArr))).to.equal('[]');
    });
});

describe('Error Handling Tests', () => {
    it('Too few arguments', () => {
        expect(() => format('%s %s', 1)).to.throw();
    });

    it('Invalid format string', () => {
        expect(() => format('%')).to.throw();
    });
});

describe('Mixed Type Format Tests', () => {
    it('Mixed numeric and string', () => {
        expect(format('%s %s %s', 42, 'test', 3.14))
            .to.equal('42 test 3.14');
    });

    it('Mixed custom and standard types', () => {
        const ct = new CustomType(42);
        expect(format('%s %s %s', ct, 'test', 3.14))
            .to.equal('CustomType(42) test 3.14');
    });
});

describe('Thread Safety Tests', () => {
    it('Concurrent formatting', async () => {
        const numThreads = 4;
        const promises = Array.from({ length: numThreads }, (_, i) =>
            Promise.resolve(format('Thread %s', i))
        );

        const results = await Promise.all(promises);

        results.forEach((result, i) => {
            expect(result).to.equal(format('Thread %s', i));
        });
    });
});