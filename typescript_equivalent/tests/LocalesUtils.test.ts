import { expect } from 'chai';
import { describe, it } from 'mocha';
import { LocalesUtils, CNumericLocalesSetter } from '../src/LocalesUtils';

describe('LocalesUtils', () => {
    describe('setLocale and getLocale', () => {
        it('should set and get locale correctly', () => {
            LocalesUtils.setLocale('fr-FR');
            expect(LocalesUtils.getLocale()).to.equal('fr-FR');

            LocalesUtils.setLocale('en-US');
            expect(LocalesUtils.getLocale()).to.equal('en-US');
        });
    });

    describe('formatNumber', () => {
        it('should format numbers according to locale', () => {
            LocalesUtils.setLocale('en-US');
            expect(LocalesUtils.formatNumber(1234.56)).to.equal('1,234.56');

            LocalesUtils.setLocale('fr-FR');
            expect(LocalesUtils.formatNumber(1234.56)).to.equal('1 234,56');
        });

        it('should handle special numbers', () => {
            LocalesUtils.setLocale('en-US');
            expect(LocalesUtils.formatNumber(0)).to.equal('0');
            expect(LocalesUtils.formatNumber(-1234.56)).to.equal('-1,234.56');
            expect(LocalesUtils.formatNumber(1e6)).to.equal('1,000,000');
        });
    });

    describe('parseNumber', () => {
        it('should parse numbers from strings', () => {
            expect(LocalesUtils.parseNumber('1234.56')).to.equal(1234.56);
            expect(LocalesUtils.parseNumber('-1234.56')).to.equal(-1234.56);
            expect(LocalesUtils.parseNumber('0')).to.equal(0);
        });

        it('should handle formatted strings', () => {
            expect(LocalesUtils.parseNumber('1,234.56')).to.equal(1234.56);
            expect(LocalesUtils.parseNumber('1 234,56')).to.equal(1234.56);
        });

        it('should handle invalid input', () => {
            expect(LocalesUtils.parseNumber('invalid')).to.be.NaN;
            expect(LocalesUtils.parseNumber('')).to.be.NaN;
        });
    });
});

describe('CNumericLocalesSetter', () => {
    it('should restore previous locale', () => {
        const originalLocale = Intl.NumberFormat().resolvedOptions().locale;

        {
            const setter = new CNumericLocalesSetter();
            expect(Intl.NumberFormat().resolvedOptions().locale).to.not.equal(originalLocale);
            setter.restore();
        }

        expect(Intl.NumberFormat().resolvedOptions().locale).to.equal(originalLocale);
    });

    it('should format numbers consistently while active', () => {
        const setter = new CNumericLocalesSetter();

        const num = 1234.56;
        const formatted = num.toString();
        expect(formatted).to.equal('1234.56');

        setter.restore();
    });

    it('should handle multiple instances correctly', () => {
        const setter1 = new CNumericLocalesSetter();
        const setter2 = new CNumericLocalesSetter();

        setter2.restore();
        setter1.restore();

        // Should still be in a valid state
        expect(() => new CNumericLocalesSetter()).not.to.throw();
    });
});