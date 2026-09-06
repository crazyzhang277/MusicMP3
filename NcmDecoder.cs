using System.Security.Cryptography;
using System.Text;
using System.Text.Json;
using System.IO;

namespace MusicToMp3;

public sealed record NcmDecodedTrack(byte[] AudioData, string Format, string Title, string Artist, string Album);

public static class NcmDecoder
{
    private static readonly byte[] Magic = "CTENFDAM"u8.ToArray();
    private static readonly byte[] AesRc4Key = Convert.FromHexString("687A4852416D736F356B496E62617857");
    private static readonly byte[] AesMetadataKey = Convert.FromHexString("2331346C6A6B5F215C5D2630553C2728");
    private static readonly byte[] Rc4Prefix = "neteasecloudmusic"u8.ToArray();
    private static readonly byte[] MetadataPrefix = "163 key(Don't modify):"u8.ToArray();

    public static NcmDecodedTrack Decode(string path)
    {
        using var stream = File.OpenRead(path);
        using var reader = new BinaryReader(stream, Encoding.UTF8, leaveOpen: false);

        if (!reader.ReadBytes(Magic.Length).SequenceEqual(Magic))
            throw new InvalidDataException("不是有效的 NCM 文件。");

        reader.ReadBytes(2);
        var encryptedKey = ReadBlock(reader, "NCM 密钥");
        var encryptedMetadata = ReadBlock(reader, "NCM 元数据");
        reader.ReadBytes(4);
        reader.ReadBytes(5);
        var cover = ReadBlock(reader, "NCM 封面");
        var encryptedAudio = reader.ReadBytes((int)(stream.Length - stream.Position));

        var rc4Key = DecodeRc4Key(encryptedKey);
        var metadata = DecodeMetadata(encryptedMetadata);
        var audio = Rc4Decrypt(rc4Key, encryptedAudio);

        var format = GetString(metadata, "format", "mp3").TrimStart('.').ToLowerInvariant();
        if (format is not ("mp3" or "flac" or "m4a" or "aac" or "wav" or "ogg"))
            format = "mp3";

        var title = GetString(metadata, "musicName", Path.GetFileNameWithoutExtension(path));
        var artist = GetArtist(metadata);
        var album = GetString(metadata, "album", "");
        _ = cover;
        return new NcmDecodedTrack(audio, format, title, artist, album);
    }

    private static byte[] ReadBlock(BinaryReader reader, string name)
    {
        var sizeBytes = reader.ReadBytes(4);
        if (sizeBytes.Length != 4)
            throw new InvalidDataException($"NCM 文件缺少{name}长度。");
        var size = BitConverter.ToInt32(sizeBytes, 0);
        if (size < 0 || size > reader.BaseStream.Length - reader.BaseStream.Position)
            throw new InvalidDataException($"NCM 文件的{name}损坏。");
        return reader.ReadBytes(size);
    }

    private static byte[] DecodeRc4Key(byte[] encrypted)
    {
        if (encrypted.Length == 0)
            throw new InvalidDataException("NCM 文件没有解码密钥。");
        var xored = encrypted.Select(b => (byte)(b ^ 0x64)).ToArray();
        var decrypted = AesEcbDecrypt(xored, AesRc4Key);
        var unpadded = RemovePkcs7(decrypted);
        if (!unpadded.AsSpan().StartsWith(Rc4Prefix))
            throw new InvalidDataException("NCM 密钥校验失败。");
        return unpadded[ Rc4Prefix.Length.. ];
    }

    private static JsonElement DecodeMetadata(byte[] encrypted)
    {
        if (encrypted.Length == 0)
            return default;
        var xored = encrypted.Select(b => (byte)(b ^ 0x63)).ToArray();
        if (!xored.AsSpan().StartsWith(MetadataPrefix))
            throw new InvalidDataException("NCM 元数据校验失败。");
        var base64 = Encoding.ASCII.GetString(xored[MetadataPrefix.Length..]);
        var decrypted = RemovePkcs7(AesEcbDecrypt(Convert.FromBase64String(base64), AesMetadataKey));
        var separator = Array.IndexOf(decrypted, (byte)':');
        if (separator < 0)
            return default;
        using var document = JsonDocument.Parse(decrypted[(separator + 1)..]);
        return document.RootElement.Clone();
    }

    private static byte[] Rc4Decrypt(byte[] key, byte[] encrypted)
    {
        if (key.Length == 0)
            throw new InvalidDataException("NCM RC4 密钥为空。");
        var box = Enumerable.Range(0, 256).Select(i => (byte)i).ToArray();
        var keyBox = new byte[256];
        var j = 0;
        for (var i = 0; i < 256; i++)
        {
            j = (j + box[i] + key[i % key.Length]) & 0xff;
            (box[i], box[j]) = (box[j], box[i]);
        }
        for (var i = 0; i < 256; i++)
        {
            j = (i + 1) & 0xff;
            var sj = box[j];
            var sjj = box[(sj + j) & 0xff];
            keyBox[i] = box[(sjj + sj) & 0xff];
        }
        var output = new byte[encrypted.Length];
        for (var i = 0; i < encrypted.Length; i++)
            output[i] = (byte)(encrypted[i] ^ keyBox[i & 0xff]);
        return output;
    }

    private static byte[] AesEcbDecrypt(byte[] encrypted, byte[] key)
    {
        if (encrypted.Length == 0 || encrypted.Length % 16 != 0)
            throw new InvalidDataException("NCM AES 数据长度无效。");
        using var aes = Aes.Create();
        aes.Key = key;
        aes.Mode = CipherMode.ECB;
        aes.Padding = PaddingMode.None;
        using var decryptor = aes.CreateDecryptor();
        return decryptor.TransformFinalBlock(encrypted, 0, encrypted.Length);
    }

    private static byte[] RemovePkcs7(byte[] data)
    {
        if (data.Length == 0)
            throw new InvalidDataException("NCM 数据为空。");
        var count = data[^1];
        if (count is 0 or > 16 || count > data.Length || data[^count..].Any(b => b != count))
            throw new InvalidDataException("NCM AES 填充校验失败。");
        return data[..^count];
    }

    private static string GetString(JsonElement json, string name, string fallback)
        => json.ValueKind == JsonValueKind.Object && json.TryGetProperty(name, out var value) && value.ValueKind == JsonValueKind.String
            ? value.GetString() ?? fallback
            : fallback;

    private static string GetArtist(JsonElement json)
    {
        if (json.ValueKind != JsonValueKind.Object || !json.TryGetProperty("artist", out var artists) || artists.ValueKind != JsonValueKind.Array)
            return "";
        return string.Join("/", artists.EnumerateArray()
            .Where(item => item.ValueKind == JsonValueKind.Array && item.GetArrayLength() > 0)
            .Select(item => item[0].GetString())
            .Where(name => !string.IsNullOrWhiteSpace(name)));
    }
}
