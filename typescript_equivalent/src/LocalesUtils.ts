// TypeScript equivalent of LocalesUtils.cpp
export class LocalesUtils {
    private static currentLocale: string = 'en-US';
    private static numberFormat: Intl.NumberFormat = new Intl.NumberFormat('en-US');

    static setLocale(locale: string): void {
        this.currentLocale = locale;
        this.numberFormat = new Intl.NumberFormat(locale, {
            useGrouping: true,
            minimumFractionDigits: 0,
            maximumFractionDigits: 20
        });
    }

    static getLocale(): string {
        return this.currentLocale;
    }

    static formatNumber(value: number): string {
        const formatted = this.numberFormat.format(value);
        // Replace non-breaking space with regular space
        return formatted.replace(/\u202F/g, ' ');
    }

    static parseNumber(str: string): number {
        // Handle different locale formats
        const cleanStr = str
            .replace(/\s/g, '') // Remove spaces
            .replace(/,(?=\d{3})/g, '') // Remove grouping commas
            .replace(/,/g, '.'); // Convert decimal comma to point
        return parseFloat(cleanStr);
    }
}

export class CNumericLocalesSetter {
    private static originalLocale: string = Intl.NumberFormat().resolvedOptions().locale;
    private static originalFormat: Intl.NumberFormat = new Intl.NumberFormat(CNumericLocalesSetter.originalLocale);
    private static instances: number = 0;
    private static originalResolvedOptions: () => Intl.ResolvedNumberFormatOptions;

    constructor() {
        CNumericLocalesSetter.instances++;
        if (CNumericLocalesSetter.instances === 1) {
            // Only modify the prototype on first instance
            CNumericLocalesSetter.originalResolvedOptions = Intl.NumberFormat.prototype.resolvedOptions;
            const originalLocale = CNumericLocalesSetter.originalLocale;
            Intl.NumberFormat.prototype.resolvedOptions = function() {
                return {
                    locale: 'fr-FR',
                    numberingSystem: 'latn',
                    style: 'decimal',
                    minimumIntegerDigits: 1,
                    minimumFractionDigits: 0,
                    maximumFractionDigits: 3,
                    useGrouping: true,
                    notation: 'standard',
                    signDisplay: 'auto'
                };
            };
        }
    }

    restore(): void {
        CNumericLocalesSetter.instances--;
        if (CNumericLocalesSetter.instances === 0) {
            // Only restore the prototype when last instance is restored
            if (CNumericLocalesSetter.originalResolvedOptions) {
                Intl.NumberFormat.prototype.resolvedOptions = CNumericLocalesSetter.originalResolvedOptions;
            }
        }
    }
}

export function isDecimalSeparatorPoint(): boolean {
    return new Intl.NumberFormat('en-US')
        .format(0.5)
        .charAt(1) === '.';
}

export function stringToDoubleDecimalPoint(str: string, pos?: { value: number }): number {
    if (!str) {
        if (pos) pos.value = 0;
        return Number.NaN;
    }

    // Skip leading whitespace
    let start = 0;
    while (start < str.length && /\s/.test(str[start])) {
        start++;
    }

    const numStr = str.slice(start);
    const num = parseFloat(numStr);

    if (pos) {
        // Find where the number parsing ended
        const numLength = numStr.match(/^[-+]?[0-9]*\.?[0-9]+([eE][-+]?[0-9]+)?/)?.[0]?.length ?? 0;
        pos.value = start + numLength;
    }

    return num;
}

export function floatToStringDecimalPoint(value: number, precision: number = -1): string {
    // Handle negative zero
    if (value === 0 && Object.is(value, -0)) {
        value = 0;  // Convert to positive zero
    }

    if (precision >= 0) {
        return value.toFixed(precision);
    }

    return value.toString();
}