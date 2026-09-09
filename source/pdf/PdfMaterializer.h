#pragma once

// ============================================================================
// PdfMaterializer - graft a chosen subset of pages of an origin PDF into a
// small, self-contained "mini-PDF" stored inside a .snb bundle.
// ============================================================================
// Part of the cross-document page transfer feature (Plan B2). When a document
// references pages from a non-primary external PDF, those pages are, on finalize
// (document close / .snbx export), grafted into a deduped bundled mini-PDF so the
// bundle becomes portable.
//
// The mini-PDF contains ONLY the referenced pages, remapped to a compact index.
// The mapping (original PDF page number -> mini-PDF page index) is returned via
// the in/out pageMap so the caller can persist it on the PdfSource entry and
// translate page references at render/search/export time.
// ============================================================================

#include <QHash>
#include <QList>
#include <QString>

class PdfMaterializer {
public:
    /**
     * @brief Append origin PDF pages into a bundled mini-PDF, extending pageMap.
     * @param originPath Absolute path to the ORIGINAL full PDF (source of pages).
     * @param bundledAbsPath Absolute path of the mini-PDF inside the bundle
     *        (created if it does not exist; appended to if it does).
     * @param originalPagesToAdd 0-based page numbers in the ORIGIN PDF to ensure
     *        are present in the mini-PDF. Pages already in pageMap are skipped.
     * @param pageMap In/out map: original PDF page number -> mini-PDF page index.
     *        Extended with newly grafted pages.
     * @param errorOut Optional: set to a human-readable message on failure.
     * @return True if the mini-PDF is present and valid afterward (including the
     *         no-op case where nothing new needed adding). False on a hard failure.
     *
     * Behavior:
     * - Only pages missing from pageMap are grafted (incremental append).
     * - If the origin PDF is unavailable, existing mapped pages are preserved and
     *   new pages are simply skipped (returns true if the mini-PDF already exists,
     *   false if it does not and nothing could be created).
     * - Writes to a temporary file and atomically replaces bundledAbsPath, so the
     *   caller MUST drop/close any provider holding the mini-PDF before calling.
     *
     * On builds without MuPDF export support this is a no-op that returns false.
     */
    static bool materialize(const QString& originPath,
                            const QString& bundledAbsPath,
                            const QList<int>& originalPagesToAdd,
                            QHash<int, int>& pageMap,
                            QString* errorOut = nullptr);
};
