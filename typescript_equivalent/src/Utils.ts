import * as process from 'node:process';

// Utility functions and helpers

export class Utils {
    static isGCodeFile(filename: string): boolean {
        return filename.toLowerCase().endsWith('.gcode');
    }

    static isJsonFile(filename: string): boolean {
        return filename.toLowerCase().endsWith('.json');
    }

    static escapeRegExp(str: string): string {
        return str.replace(/[.*+?^${}()|[\]\\]/g, '\\$&');
    }

    static normalizeLineEndings(str: string): string {
        return str.replace(/\r\n/g, '\n').replace(/\r/g, '\n');
    }

    static splitLines(str: string): string[] {
        return str.split(/\r\n|\r|\n/);
    }

    static trimString(str: string): string {
        return str.trim();
    }

    static isWhitespace(c: string): boolean {
        return /\s/.test(c);
    }

    static isEndOfLine(c: string): boolean {
        return c === '\r' || c === '\n' || c === '';
    }

    static isEndOfGCodeLine(c: string): boolean {
        return c === ';' || Utils.isEndOfLine(c);
    }

    static isEndOfWord(c: string): boolean {
        return Utils.isWhitespace(c) || Utils.isEndOfGCodeLine(c);
    }

    static skipWord(str: string, startIndex: number = 0): number {
        let i = startIndex;
        while (i < str.length && !Utils.isEndOfWord(str[i])) {
            i++;
        }
        return i;
    }

    static skipWhitespaces(str: string, startIndex: number = 0): number {
        let i = startIndex;
        while (i < str.length && Utils.isWhitespace(str[i])) {
            i++;
        }
        return i;
    }

    static isWindows(): boolean {
        return process.platform === 'win32';
    }

    static isMac(): boolean {
        return process.platform === 'darwin';
    }

    static isLinux(): boolean {
        return process.platform === 'linux';
    }

    static normalizePath(path: string): string {
        return path.replace(/\\/g, '/');
    }

    static getHomeDir(): string {
        return process.env.HOME || process.env.USERPROFILE || '';
    }

    static getAppDataDir(): string {
        if (this.isWindows()) {
            return process.env.APPDATA || '';
        }
        if (this.isMac()) {
            return `${this.getHomeDir()}/Library/Application Support`;
        }
        return `${this.getHomeDir()}/.local/share`;
    }
}