import { expect } from 'chai';
import * as chai from 'chai';

declare global {
    namespace NodeJS {
        interface Global {
            expect: Chai.ExpectStatic;
        }
    }
}

// Add any global test setup here
(global as any).expect = expect;