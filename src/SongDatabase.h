#pragma once

struct sqlite3;
struct sqlite3_stmt;

namespace VSRG
{
    class Song;
}

class SongDatabase
{
private:
    sqlite3 *db;
    sqlite3_stmt *st_IDQuery,
        *st_SngInsertQuery,
        *st_DiffInsertQuery,
        *st_FilenameQuery,
        *st_FilenameInsertQuery,
        *st_DiffIDQuery,
        *st_LMTQuery,
        *st_DelDiffsQuery,
        *st_GetSongInfo,
        *st_GetDiffInfo,
        *st_GetFileInfo,
        *st_UpdateLMT,
        *st_GetDiffIDFile,
        *st_DiffUpdateQuery,
        *st_GetDiffFilename,
        *st_GetSIDFromFilename,
        *st_GetLastSongID,
        *st_GetDiffAuthor,
        *st_GetPreviewInfo,
        *st_GetStageFile;

    // Returns the ID.
    int InsertFilename(std::filesystem::path Fn);
    bool DifficultyExists(int FileID, std::string DifficultyName, int *IDOut = NULL);
public:

    SongDatabase(std::string Database);
    ~SongDatabase();

    void ClearDifficulties(int SongID);
    bool CacheNeedsRenewal(std::filesystem::path Dir);
    void AddDifficulty(int SongID, std::filesystem::path Filename, Game::Song::Difficulty* Diff, int Mode);

    void GetPreviewInfo(int SongID, std::string &Filename, float &PreviewStart);

    // Difficulty information
    std::string GetDifficultyFilename(int DiffID);
    std::string GetArtistForDifficulty(int DiffID);
    std::string GetStageFile(int DiffID);

    int GetSongIDForFile(std::filesystem::path File, VSRG::Song* In);

    void GetSongInformation7K(int ID, VSRG::Song* Out);

    void StartTransaction();
    void EndTransaction();
};