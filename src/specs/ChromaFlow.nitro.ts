import { type HybridObject } from 'react-native-nitro-modules'

export interface ChromaFlow extends HybridObject<{ ios: 'c++', android: 'c++' }> {
    encode(
        data: ArrayBuffer,
        colorNumber: number,
        moduleSize: number,
        symbolWidth: number,
        symbolHeight: number,
        eccLevel: number
    ): ArrayBuffer

    decode(pngData: ArrayBuffer): ArrayBuffer
}