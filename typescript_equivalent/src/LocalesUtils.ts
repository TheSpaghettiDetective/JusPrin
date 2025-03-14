// TypeScript equivalent of LocalesUtils.cpp
export namespace Slic3r {
    // Class to handle numeric locale settings
    export class CNumericLocalesSetter {
        private originalLocale: string;

        constructor() {
            // Store the current locale
            this.originalLocale = Intl.NumberFormat().resolvedOptions().locale;
            // Set to 'en-US' for decimal point consistency
            Intl.NumberFormat('en-US');
        }

        dispose() {
            // Restore original locale
            Intl.NumberFormat(this.originalLocale);
        }
    }

    // Check if decimal separator is a point
    export function isDecimalSeparatorPoint(): boolean {
        return new Intl.NumberFormat('en-US')
            .format(0.5)
            .charAt(1) === '.';
    }

    // Convert string to double ensuring decimal point
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

    // Convert float to string with decimal point
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
}