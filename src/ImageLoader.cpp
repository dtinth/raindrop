#include "pch.h"

#include "Logging.h"
#include "Configuration.h"

#include "Image.h"
#include "ImageLoader.h"

std::mutex LoadMutex;
std::map<std::filesystem::path, Image*> ImageLoader::Textures;
std::map<std::filesystem::path, ImageLoader::UploadData> ImageLoader::PendingUploads;

void Image::CreateTexture()
{
    if (texture == -1 || !IsValid)
    {
        glGenTextures(1, &texture);
        glBindTexture(GL_TEXTURE_2D, texture);

        LastBound = this;
        IsValid = true;
    }
}

void Image::BindNull()
{
    glBindTexture(GL_TEXTURE_2D, 0);
    LastBound = NULL;
}

void Image::Bind()
{
    if (IsValid && texture != -1)
    {
        if (LastBound != this)
        {
            glBindTexture(GL_TEXTURE_2D, texture);
            LastBound = this;
        }
    }
}

void Image::Destroy() // Called at destruction time
{
    if (IsValid && texture != -1)
    {
        glDeleteTextures(1, &texture);
        IsValid = false;
        texture = -1;
    }
}

void Image::SetTextureData(ImageData *ImgInfo, bool Reassign)
{
    if (Reassign) Destroy();

    CreateTexture(); // Make sure our texture exists.
    Bind();

    if (ImgInfo->Data == nullptr && !Reassign)
    {
        return;
    }

    if (!TextureAssigned || Reassign) // We haven't set any data to this texture yet, or we want to regenerate storage
    {
        TextureAssigned = true;
        auto Dir = ImgInfo->Filename.filename().string();

        glPixelStorei(GL_UNPACK_ALIGNMENT, 4);
        if (!Configuration::TextureParameterExists(Dir, "gen-mipmap") || Configuration::GetTextureParameter(Dir, "gen-mipmap") == "true")
            glTexParameteri(GL_TEXTURE_2D, GL_GENERATE_MIPMAP, GL_TRUE);

        GLint param;
        switch (ImgInfo->WrapMode)
        {
        case ImageData::WM_CLAMP_TO_EDGE:
            param = GL_CLAMP_TO_EDGE;
            break;
        case ImageData::WM_REPEAT:
            param = GL_REPEAT;
            break;
        default:
            param = GL_CLAMP_TO_EDGE;
        }

        GLint wrapS = param, wrapT = param;
        if (Configuration::GetTextureParameter(Dir, "wrap-s") == "clamp-edge")
            wrapS = GL_CLAMP_TO_EDGE;
        else if (Configuration::GetTextureParameter(Dir, "wrap-s") == "repeat")
            wrapS = GL_REPEAT;
        else if (Configuration::GetTextureParameter(Dir, "wrap-s") == "clamp-border")
            wrapS = GL_CLAMP_TO_BORDER;
        else if (Configuration::GetTextureParameter(Dir, "wrap-s") == "repeat-mirrored")
            wrapS = GL_MIRRORED_REPEAT;

        if (Configuration::GetTextureParameter(Dir, "wrap-t") == "clamp-edge")
            wrapT = GL_CLAMP_TO_EDGE;
        else if (Configuration::GetTextureParameter(Dir, "wrap-t") == "repeat")
            wrapT = GL_REPEAT;
        else if (Configuration::GetTextureParameter(Dir, "wrap-t") == "clamp-border")
            wrapT = GL_CLAMP_TO_BORDER;
        else if (Configuration::GetTextureParameter(Dir, "wrap-t") == "repeat-mirrored")
            wrapT = GL_MIRRORED_REPEAT;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_S, wrapS);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_WRAP_T, wrapT);

        GLint minparam;
        switch (ImgInfo->ScalingMode)
        {
        case ImageData::SM_LINEAR:
            minparam = GL_LINEAR;
            param = GL_LINEAR;
            break;
        case ImageData::SM_MIPMAP:
            minparam = GL_LINEAR_MIPMAP_LINEAR;
            param = GL_LINEAR;
            break;
        case ImageData::SM_NEAREST:
            minparam = GL_NEAREST;
            param = GL_NEAREST;
            break;
        default:
            minparam = GL_LINEAR_MIPMAP_LINEAR;
            param = GL_LINEAR;
        }

        GLint minp = minparam, maxp = param;
        if (Configuration::GetTextureParameter(Dir, "minfilter") == "linear")
            minp = GL_LINEAR;
        else if (Configuration::GetTextureParameter(Dir, "minfilter") == "nearest")
            minp = GL_NEAREST;
        else if (Configuration::GetTextureParameter(Dir, "minfilter") == "linear-mipmap-linear")
            minp = GL_LINEAR_MIPMAP_LINEAR;
        else if (Configuration::GetTextureParameter(Dir, "minfilter") == "linear-mipmap-nearest")
            minp = GL_LINEAR_MIPMAP_NEAREST;
        else if (Configuration::GetTextureParameter(Dir, "minfilter") == "nearest-mipmap-nearest")
            minp = GL_NEAREST_MIPMAP_NEAREST;
        else if (Configuration::GetTextureParameter(Dir, "minfilter") == "nearest-mipmap-linear")
            minp = GL_NEAREST_MIPMAP_LINEAR;

        if (Configuration::GetTextureParameter(Dir, "maxfilter") == "linear")
            maxp = GL_LINEAR;
        else if (Configuration::GetTextureParameter(Dir, "maxfilter") == "nearest")
            maxp = GL_NEAREST;
        else if (Configuration::GetTextureParameter(Dir, "maxfilter") == "linear-mipmap-linear")
            maxp = GL_LINEAR_MIPMAP_LINEAR;
        else if (Configuration::GetTextureParameter(Dir, "maxfilter") == "linear-mipmap-nearest")
            maxp = GL_LINEAR_MIPMAP_NEAREST;
        else if (Configuration::GetTextureParameter(Dir, "maxfilter") == "nearest-mipmap-nearest")
            maxp = GL_NEAREST_MIPMAP_NEAREST;
        else if (Configuration::GetTextureParameter(Dir, "maxfilter") == "nearest-mipmap-linear")
            maxp = GL_NEAREST_MIPMAP_LINEAR;

        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MIN_FILTER, minp);
        glTexParameteri(GL_TEXTURE_2D, GL_TEXTURE_MAG_FILTER, maxp);
        glTexImage2D(GL_TEXTURE_2D, 0, GL_RGBA, ImgInfo->Width, ImgInfo->Height, 0, GL_RGBA, GL_UNSIGNED_BYTE, ImgInfo->Data);
    }
    else // We did, so let's update instead.
    {
        glTexSubImage2D(GL_TEXTURE_2D, 0, 0, 0, ImgInfo->Width, ImgInfo->Height, GL_RGBA, GL_UNSIGNED_BYTE, ImgInfo->Data);
    }

    w = ImgInfo->Width;
    h = ImgInfo->Height;
    fname = ImgInfo->Filename;
}

void Image::Assign(std::filesystem::path Filename, ImageData::EScalingMode ScaleMode,
    ImageData::EWrapMode WrapMode,
    bool Regenerate)
{
    ImageData Ret;
    CreateTexture();

    Ret = ImageLoader::GetDataForImage(Filename);
    Ret.ScalingMode = ScaleMode;
    Ret.WrapMode = WrapMode;
    SetTextureData(&Ret, Regenerate);
    fname = Filename;
    free(Ret.Data);
}

ImageLoader::ImageLoader()
{
}

ImageLoader::~ImageLoader()
{
    // unload ALL the images.
}

void ImageLoader::InvalidateAll()
{
    for (auto i = Textures.begin(); i != Textures.end(); ++i)
    {
        i->second->IsValid = false;
    }
}

void ImageLoader::UnloadAll()
{
    InvalidateAll();

    for (auto i = Textures.begin(); i != Textures.end(); i++)
    {
        glDeleteTextures(1, &i->second->texture);
    }
}

void ImageLoader::DeleteImage(Image* &ToDelete)
{
    if (ToDelete)
    {
        Textures.erase(Textures.find(ToDelete->fname));
        delete ToDelete;
        ToDelete = nullptr;
    }
}

Image* ImageLoader::InsertImage(std::filesystem::path Name, ImageData *imgData)
{
    Image* I;

    if (!imgData || imgData->Data == nullptr) return nullptr;

    if (Textures.find(Name) == Textures.end())
        I = (Textures[Name] = new Image());
    else
        I = Textures[Name];

    I->SetTextureData(imgData);
    I->fname = Name;

    return I;
}

template<typename Stream>
auto open_image(Stream&& in)
{
    using namespace boost::gil;

    rgba8_image_t img;
    do
    {
        try
        {
            try
            {
                read_and_convert_image(in, img, png_tag());
                break;
            }
            catch (std::ios_base::failure) {}

            in.clear();
            in.seekg(0);
            try
            {
                read_and_convert_image(in, img, jpeg_tag());
                break;
            }
            catch (std::ios_base::failure) {}

            in.clear();
            in.seekg(0);
            try
            {
                read_and_convert_image(in, img, targa_tag());
                break;
            }
            catch (std::ios_base::failure) {}

            in.clear();
            in.seekg(0);
            read_and_convert_image(in, img, bmp_tag());
        }
        catch (...)
        {
            Log::Printf("Could not load image");
        }
    } while (false);

    auto v = view(img);
    using pixel = decltype(v)::value_type;
    auto data = new pixel[v.width() * v.height()];
    copy_pixels(v, interleaved_view(v.width(), v.height(), data,
        v.width() * sizeof(pixel)));

    ImageData out;
    out.Data = data;
    out.Width = v.width();
    out.Height = v.height();

    return out;
}

ImageData ImageLoader::GetDataForImage(std::filesystem::path filename)
{
    auto file = std::ifstream{ filename, std::ios::binary };
    if (!file.is_open())
    {
        Log::Printf("Could not open file \"%s\".\n", filename);
        return{};
    }

    auto out = open_image(file);
    out.Filename = filename;

    return out;
}

ImageData ImageLoader::GetDataForImageFromMemory(const unsigned char* const buffer, size_t len)
{
    auto file = std::stringstream{ std::stringstream::in |
        std::stringstream::out | std::stringstream::binary };
    file.write((const char*)buffer, len);

    auto out = open_image(file);

    return out;
}

Image* ImageLoader::Load(std::filesystem::path filename)
{
	if (std::filesystem::is_directory(filename)) return NULL;
    if (Textures.find(filename) != Textures.end() && Textures[filename]->IsValid)
    {
        return Textures[filename];
    }
    else
    {
        ImageData ImgData = GetDataForImage(filename);
        Image* Ret = InsertImage(filename, &ImgData);

        free(ImgData.Data);
        Image::LastBound = Ret;

        return Ret;
    }
    return 0;
}

void ImageLoader::AddToPending(std::filesystem::path Filename)
{
    UploadData New;
    if (Textures.find(Filename) == Textures.end())
    {
        auto d = GetDataForImage(Filename);
        New.Data = d.Data;
        New.Width = d.Width;
        New.Height = d.Height;
        LoadMutex.lock();
        PendingUploads.insert(std::pair<std::filesystem::path, UploadData>(Filename, New));
        LoadMutex.unlock();
    }
}

/* For multi-threaded loading. */
void ImageLoader::LoadFromManifest(char** Manifest, int Count, std::string Prefix)
{
    for (int i = 0; i < Count; i++)
    {
        const char* FinalFilename = (Prefix + Manifest[i]).c_str();
        AddToPending(FinalFilename);
    }
}

void ImageLoader::UpdateTextures()
{
    if (PendingUploads.size() && LoadMutex.try_lock())
    {
        for (auto i = PendingUploads.begin(); i != PendingUploads.end(); i++)
        {
            ImageData imgData;
            imgData.Data = i->second.Data;
            imgData.Width = i->second.Width;
            imgData.Height = i->second.Height;

            Image::LastBound = InsertImage(i->first, &imgData);

            free(imgData.Data);
        }

        PendingUploads.clear();
        LoadMutex.unlock();
    }

    if (Textures.size())
    {
        for (auto i = Textures.begin(); i != Textures.end();)
        {
            if (i->second->IsValid) /* all of them are valid */
                break;

            if (Load(i->first) == nullptr) // If we failed loading it no need to try every. single. time.
            {
                i = Textures.erase(i);
                continue;
            }

            ++i;
        }
    }
}