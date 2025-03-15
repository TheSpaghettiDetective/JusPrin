// Type declarations for external modules

declare module 'nlohmann' {
    export interface json {
        [key: string]: any;
    }
}

declare module 'boost' {
    // Add boost type declarations as needed
    export interface property_tree {
        ptree: any;
    }
}

declare module 'node:fs' {
    export function readFileSync(path: string, encoding: string): string;
    export function writeFileSync(path: string, data: string): void;
    export function existsSync(path: string): boolean;
}

declare module 'node:path' {
    export function join(...paths: string[]): string;
    export function dirname(path: string): string;
    export function basename(path: string): string;
    export function extname(path: string): string;
}