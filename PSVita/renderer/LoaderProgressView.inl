// Included by LoaderUI.h inside VitaBrowser after shared UI helpers.
inline void drawLoaderProgress(const RuntimeSettings& settings, LoaderPage page,
                               const ProgressUI::Model& model, float activity)
{
    (void)settings; (void)page; (void)activity;
    drawLoaderStatusScreen(model.eyebrow,model.title,model.detail);
}

inline void presentLoaderProgress(const RuntimeSettings& settings, LoaderPage page,
                                  const ProgressUI::Model& model)
{
    drawLoaderProgress(settings,page,model,0.f);
    vglSwapBuffers(GL_FALSE);
}
