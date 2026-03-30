import { type HybridObject } from 'react-native-nitro-modules'

interface ChromaFlow extends HybridObject<{ android: 'c++' }> {
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