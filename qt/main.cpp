#include <QtWidgets>
#include <QProcess>
#include <QSaveFile>
#include <QCryptographicHash>
#include <QRegularExpression>
#include <QUuid>
#include <algorithm>
#include <functional>
#ifdef Q_OS_WIN
#include <windows.h>
#include <dwmapi.h>
#endif

struct Step { QString title, program; QStringList arguments; int maximumExit = 0; };
struct SceneIdentity { QString id, title, method; };

// Process boundaries isolate old IKVM/.NET and current Java formats from Qt.
// No parsing/exporting code from the working legacy backend is modified.
class Studio : public QMainWindow {
    QLineEdit *game, *blender, *java, *archive, *output, *globals, *colorNode, *ueCharacter;
    QTabWidget *tabs;
    QListWidget *levels, *skies, *models;
    QTreeWidget *modelHierarchy, *modelProperties;
    QComboBox *modelVariant;
    QCheckBox *glb;
    QLabel *modelSummary;
    QFormLayout *modelChoices;
    QMap<QString,QComboBox*> modelChoiceEditors;
    QPlainTextEdit *log;
    QLabel *status;
    QPushButton *run, *cancel, *openResult;
    QCheckBox *ueNames, *fbx;
    QProgressBar *progress;
    QProcess process;
    QProcess *inspectProcess = nullptr;
    QProcess *previewProcess = nullptr;
    QWidget *previewHost = nullptr;
    QWidget *previewOverlay = nullptr;
    QLabel *previewPlaceholder = nullptr;
#ifdef Q_OS_WIN
    HTHUMBNAIL previewThumbnail = nullptr;
    HWND previewSourceWindow = nullptr;
#endif
    QList<Step> pending;
    Step active;
    QFile logFile;
    QString resultPath, ueResultPath, bundlePath, mode, selectedOutput, stagingPath, exportPrefix, previousPrefix, currentSource;
    QString batchModelVariant;
    QString batchUeFbxClips;
    SceneIdentity currentScene;
    QHash<QString,QString> sceneNames,sceneFingerprints;
    bool batch = false, busy = false, cancelled = false, openModelInBlender = false;
    QSettings settings;
    QJsonObject defaults;

    QString bundledBlender() const {return QDir(QCoreApplication::applicationDirPath()).filePath("tools/blender/blender.exe");}
    QString bundledJava() const {return QDir(QCoreApplication::applicationDirPath()).filePath("runtime/bin/java.exe");}
    QString blenderExecutable() const {return blender->text().isEmpty()?bundledBlender():blender->text();}
    QString javaExecutable() const {
        if(!java->text().isEmpty())return java->text();
        const QString gameRuntime=QDir(game->text()).filePath("java_vm");
        const QString gameJava=QDir(gameRuntime).filePath("bin/java.exe");
        if(QFileInfo::exists(gameJava)&&!QFileInfo::exists(QDir(gameRuntime).filePath("lib/ext")))return gameJava;
        const QString cached=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/game-java/bin/java.exe";
        if(QFileInfo::exists(cached))return cached;
        if(QFileInfo::exists(bundledJava()))return bundledJava();
        return gameJava;
    }

#ifdef Q_OS_WIN
    struct PreviewWindowSearch { DWORD pid; HWND result = nullptr; qint64 area = 0; };
    static BOOL CALLBACK findPreviewWindow(HWND window, LPARAM parameter) {
        auto *search=reinterpret_cast<PreviewWindowSearch*>(parameter);
        DWORD pid=0;GetWindowThreadProcessId(window,&pid);
        if(pid!=search->pid||!IsWindowVisible(window)||GetWindow(window,GW_OWNER))return TRUE;
        RECT bounds{};if(!GetWindowRect(window,&bounds))return TRUE;
        const qint64 area=qMax(0L,bounds.right-bounds.left)*qint64(qMax(0L,bounds.bottom-bounds.top));
        if(area>search->area){search->area=area;search->result=window;}
        return TRUE;
    }
#endif

    QLineEdit *pathRow(QFormLayout *form, const QString &title, const QString &key,
                      bool directory, const QString &fallback = {}) {
        auto row = new QWidget;
        auto layout = new QHBoxLayout(row); layout->setContentsMargins(0,0,0,0);
        auto edit = new QLineEdit(settings.value(key, defaults.value(key).toString(fallback)).toString());
        auto browse = new QPushButton("Browse…");
        layout->addWidget(edit); layout->addWidget(browse);
        form->addRow(title, row);
        connect(browse, &QPushButton::clicked, this, [=]{
            QString selected = directory ? QFileDialog::getExistingDirectory(this,title,edit->text())
                : QFileDialog::getOpenFileName(this,title,edit->text());
            if(!selected.isEmpty()) {
                edit->setText(QDir::toNativeSeparators(selected));
                settings.setValue(key,edit->text());
            }
        });
        connect(edit, &QLineEdit::editingFinished, this, [=]{settings.setValue(key,edit->text());});
        return edit;
    }
    void append(const QString &text) {
        log->moveCursor(QTextCursor::End); log->insertPlainText(text);
        log->ensureCursorVisible();
        if(logFile.isOpen()){logFile.write(text.toUtf8()); logFile.flush();}
    }
    static bool replaceFile(const QString &source,const QString &destination,QString &error) {
        QFile input(source);
        if(!input.open(QIODevice::ReadOnly)){error="Cannot read "+source+": "+input.errorString();return false;}
        if(!QDir().mkpath(QFileInfo(destination).absolutePath())){error="Cannot create output directory for "+destination;return false;}
        QSaveFile output(destination);
        if(!output.open(QIODevice::WriteOnly)){error="Cannot replace "+destination+": "+output.errorString();return false;}
        while(!input.atEnd()){
            QByteArray chunk=input.read(1024*1024);
            if(chunk.isEmpty()||output.write(chunk)!=chunk.size()){
                output.cancelWriting();error="Copy failed for "+destination;return false;
            }
        }
        if(!output.commit()){error="Could not finish replacing "+destination+": "+output.errorString();return false;}
        return true;
    }
    void clearStage() {
        if(stagingPath.isEmpty())return;
        // The stage is an app-generated UUID child of the selected export folder.
        const QString parent=QFileInfo(stagingPath).absoluteDir().canonicalPath();
        if(parent==QDir(selectedOutput).canonicalPath() && QFileInfo(stagingPath).fileName().startsWith(".trs-stage-"))
            QDir(stagingPath).removeRecursively();
        stagingPath.clear();
    }
    void loadSceneCatalog() {
        auto read=[this](const QString &file,QHash<QString,QString> &target){
            QFile input(backend(file));
            if(!input.open(QIODevice::ReadOnly))return;
            while(!input.atEnd()){
                const QString row=QString::fromUtf8(input.readLine()).trimmed();
                if(row.startsWith('#'))continue;
                const int tab=row.indexOf('\t');
                if(tab>0)target.insert(row.left(tab),row.mid(tab+1));
            }
        };
        read("scene-names.tsv",sceneNames);
        read("scene-fingerprints.tsv",sceneFingerprints);
    }
    SceneIdentity identifyScene(const QString &source) const {
        const QString base=QFileInfo(source).completeBaseName();
        QString id,method;
        if(sceneNames.contains(base)){id=base;method="archive filename";}
        else {
            QFile input(source);
            if(input.open(QIODevice::ReadOnly)){
                QCryptographicHash hash(QCryptographicHash::Sha256);
                if(hash.addData(&input)){
                    const QString sha=QString::fromLatin1(hash.result().toHex());
                    id=sceneFingerprints.value(sha);if(!id.isEmpty())method="archive content match";
                }
            }
        }
        return {id,sceneNames.value(id),method};
    }
    static QString prefixFor(const QString &kind,const QString &source,const QString &gamePath,const QString &displayTitle={}) {
        const QString identity=kind=="animation"?QFileInfo(gamePath).absoluteFilePath():QFileInfo(source).absoluteFilePath();
        QString slug=kind=="animation"?"knight-animations":displayTitle.isEmpty()?QFileInfo(source).completeBaseName().toLower():displayTitle.toLower();
        if(kind=="skybox")slug=QFileInfo(source).dir().dirName().toLower()+"-"+slug;
        slug.replace(QRegularExpression("[^a-z0-9]+"),"-");
        slug=slug.left(48);while(slug.endsWith('-'))slug.chop(1);
        if(slug.isEmpty())slug="source";
        const QString hash=QString::fromLatin1(QCryptographicHash::hash(identity.toUtf8(),QCryptographicHash::Sha256).toHex().left(10));
        return kind+"-"+slug+"-"+hash;
    }
    QString publishedName(const QString &relative) const {
        const QString file=QFileInfo(relative).fileName();
        if(file.startsWith("trs-texture-") && file.endsWith(".png"))return file;
        if(file=="UE-Retargeted.blend")return exportPrefix+".ue.blend";
        if(file.endsWith(".blend",Qt::CaseInsensitive))return exportPrefix+".blend";
        if(file.endsWith(".glb",Qt::CaseInsensitive))return exportPrefix+".glb";
        if(relative.startsWith("ue-fbx/"))return exportPrefix+".ue."+file;
        if(relative.startsWith("fbx/"))return exportPrefix+"."+file;
        return exportPrefix+"."+file;
    }
    void cleanPreviousPrefix() {
        if(previousPrefix.isEmpty()||previousPrefix==exportPrefix)return;
        QFile oldManifest(QDir(selectedOutput).filePath(previousPrefix+".manifest.json"));
        if(oldManifest.open(QIODevice::ReadOnly)){
            const QJsonArray files=QJsonDocument::fromJson(oldManifest.readAll()).array();oldManifest.close();
            for(const QJsonValue &value:files){
                const QString file=value.toString();
                if(file.startsWith(previousPrefix+".") && !file.contains("..") && !QDir::isAbsolutePath(file))
                    QFile::remove(QDir(selectedOutput).filePath(file));
            }
            QFile::remove(oldManifest.fileName());
        }
        QFile oldLog(QDir(selectedOutput).filePath(previousPrefix+".export.log"));
        if(oldLog.open(QIODevice::ReadOnly)){
            const QByteArray header=oldLog.read(4096);oldLog.close();
            if(header.contains("Source: "+currentSource.toUtf8()+"\n")){
                QFile::remove(oldLog.fileName());
                QFile::remove(QDir(selectedOutput).filePath(previousPrefix+".last-failed-report.json"));
            }
        }
    }
    void cleanMatchingLegacyScene() {
        if(mode=="animation")return;
        QFile oldBundle(QDir(selectedOutput).filePath("scene.json"));
        if(!oldBundle.open(QIODevice::ReadOnly))return;
        const QString source=QJsonDocument::fromJson(oldBundle.readAll()).object().value("source").toString();
        oldBundle.close();
        auto normalized=[](QString path){return QDir::cleanPath(path.replace('\\','/'));};
        if(source.isEmpty() || normalized(source).compare(normalized(currentSource),Qt::CaseInsensitive)!=0)return;
        for(const QString &file:{"scene.json","scene-report.json","blender-scene-validation.json","Level.blend","Skybox.blend","export.log","export-manifest.json"})
            QFile::remove(QDir(selectedOutput).filePath(file));
        QDir textures(QDir(selectedOutput).filePath("textures"));
        for(const QString &file:textures.entryList({"recolored_*.png"},QDir::Files))textures.remove(file);
        QDir(selectedOutput).rmdir("textures");
        append("Removed older generic scene files for this source.\n");
    }
    bool publish(QString &error) {
        if(stagingPath.isEmpty()){error="Export staging location is missing.";return false;}
        QStringList files;
        QDirIterator it(stagingPath,QDir::Files,QDirIterator::Subdirectories);
        while(it.hasNext()){it.next();files<<QDir(stagingPath).relativeFilePath(it.filePath());}
        std::sort(files.begin(),files.end(),[](const QString &a,const QString &b){
            const bool blendA=a.endsWith(".blend",Qt::CaseInsensitive),blendB=b.endsWith(".blend",Qt::CaseInsensitive);
            return blendA==blendB?a<b:!blendA;
        });
        if(files.isEmpty()){error="Exporter produced no files.";return false;}
        QStringList oldFiles;
        const QString manifestPath=QDir(selectedOutput).filePath(exportPrefix+".manifest.json");
        QFile manifest(manifestPath);
        if(manifest.open(QIODevice::ReadOnly)){
            for(const auto &entry:QJsonDocument::fromJson(manifest.readAll()).array())oldFiles<<entry.toString();
            manifest.close();
        }
        QStringList published;
        for(const QString &relative:files){
            const QString name=publishedName(relative);
            if(!replaceFile(QDir(stagingPath).filePath(relative),QDir(selectedOutput).filePath(name),error))return false;
            published<<name;
        }
        for(const QString &old:oldFiles)if(!published.contains(old) && old.startsWith(exportPrefix+".") && !old.contains("..") && !QDir::isAbsolutePath(old)){
            const QString target=QDir(selectedOutput).filePath(old);
            if(QFileInfo(target).isFile() && !QFile::remove(target))append("Could not remove stale export file: "+target+"\n");
        }
        QFile::remove(QDir(selectedOutput).filePath(exportPrefix+".last-failed-report.json"));
        QJsonArray entries;for(const QString &name:published)entries.append(name);
        QSaveFile newManifest(manifestPath);
        if(!newManifest.open(QIODevice::WriteOnly) || newManifest.write(QJsonDocument(entries).toJson())<0 || !newManifest.commit()){
            error="Could not update the export manifest.";return false;
        }
        cleanMatchingLegacyScene();
        cleanPreviousPrefix();
        resultPath=QDir(selectedOutput).filePath(exportPrefix+".blend");
        if(!ueResultPath.isEmpty())ueResultPath=QDir(selectedOutput).filePath(exportPrefix+".ue.blend");
        bundlePath=QDir(selectedOutput).filePath(exportPrefix+"."+(mode=="animation"?"knight-animations.json":"scene.json"));
        append(QString("Updated %1 named export files in %2\n").arg(published.size()).arg(selectedOutput));
        return true;
    }
    void finish(bool success, const QString &message) {
        if(!busy) return;
        if(!success && !stagingPath.isEmpty()){
            QString reportName=mode=="animation"?"export-report.json":"scene-report.json";
            QString report=QDir(stagingPath).filePath(reportName),error;
            if(QFileInfo::exists(report))replaceFile(report,QDir(selectedOutput).filePath(exportPrefix+".last-failed-report.json"),error);
            append("Previous completed export files remain available.\n");
        }
        append("\n"+message+"\n"); pending.clear(); busy=false;
        status->setText(message); run->setEnabled(true); cancel->setEnabled(false);
        tabs->setEnabled(true); progress->setRange(0,1); progress->setValue(success?1:0);
        openResult->setEnabled(success); logFile.close();
        clearStage();
        if(openModelInBlender){
            openModelInBlender=false;
            if(success && mode=="model" && !QProcess::startDetached(blenderExecutable(),{resultPath}))
                status->setText("Model exported, but Blender could not open it: "+resultPath);
        }
        if(batch) QTimer::singleShot(0,qApp,[success]{QCoreApplication::exit(success?0:1);});
    }
    void next() {
        if(cancelled){finish(false,"Cancelled. The selected folder remains available for retry.");return;}
        if(pending.isEmpty()) {
            if(!QFileInfo::exists(resultPath)){finish(false,"Backend exited without the expected .blend file.");return;}
            if(!ueResultPath.isEmpty()&&!QFileInfo::exists(ueResultPath)){
                finish(false,"Retargeting exited without the expected UE .blend file.");return;
            }
            QFile report(QFileInfo(bundlePath).dir().filePath(mode=="animation"?"export-report.json":"scene-report.json"));
            int warnings = 0;
            if(report.open(QIODevice::ReadOnly)) {
                auto doc=QJsonDocument::fromJson(report.readAll()).object();
                warnings=doc.value(mode=="animation"?"skipped":"issues").toArray().size();
                report.close();
            }
            QString error;
            if(!publish(error)){finish(false,"Export could not replace the selected files: "+error);return;}
            QString message=QString("Export complete — %1 reported limitations. %2").arg(warnings).arg(resultPath);
            if(!ueResultPath.isEmpty())message+="\nUE retarget: "+ueResultPath;
            finish(true,message);return;
        }
        active=pending.takeFirst();status->setText(active.title);
        append("\n=== "+active.title+" ===\n");
        // Each argument is passed directly, never interpreted by a shell.
        process.setProgram(active.program); process.setArguments(active.arguments);
        process.setWorkingDirectory(QCoreApplication::applicationDirPath());process.start();
    }
    QString backend(const QString &file) const {
        return QDir(QCoreApplication::applicationDirPath()).filePath("backend/"+file);
    }
    QString selection(QListWidget *list) const {
        return list->currentItem()&&!list->currentItem()->isHidden()?list->currentItem()->data(Qt::UserRole).toString():QString();
    }
    void populate(QListWidget *list, const QString &directory, bool sky) {
        list->clear();
        QDirIterator it(directory,QDir::Files,QDirIterator::Subdirectories);
        const QDir root(directory);
        while(it.hasNext()) {
            auto path=it.next(); auto file=it.fileInfo();
            if(sky ? file.suffix().compare("dat",Qt::CaseInsensitive)!=0 :
                (!file.suffix().isEmpty() && file.suffix().compare("dat",Qt::CaseInsensitive)!=0)) continue;
            QString label=root.relativeFilePath(path);
            if(!sky){const SceneIdentity identity=identifyScene(path);
                if(!identity.title.isEmpty())label=identity.title+" ["+identity.id+"] — "+label;}
            auto item=new QListWidgetItem(label,list);item->setData(Qt::UserRole,path);
        }
        list->sortItems();
        status->setText(QString("%1 files found. Select a source to export.").arg(list->count()));
    }
    QWidget *sourcePage(const QString &help, QListWidget *&list, bool sky) {
        auto page=new QWidget;auto layout=new QVBoxLayout(page);
        auto label=new QLabel(help);label->setWordWrap(true);layout->addWidget(label);
        auto filter=new QLineEdit;filter->setPlaceholderText("Filter by file or level name…");layout->addWidget(filter);
        list=new QListWidget;layout->addWidget(list,1);
        auto reload=new QPushButton(sky?"Scan game skyboxes":"Scan archive folder");layout->addWidget(reload);
        auto target=list;
        connect(reload,&QPushButton::clicked,this,[=]{populate(target,sky?QDir(game->text()).filePath("rsrc/world/skybox"):archive->text(),sky);});
        connect(filter,&QLineEdit::textChanged,this,[target](const QString &s){for(int i=0;i<target->count();i++)target->item(i)->setHidden(!target->item(i)->text().contains(s,Qt::CaseInsensitive));});
        auto choose=new QPushButton("Choose source file…");layout->addWidget(choose);
        connect(choose,&QPushButton::clicked,this,[=]{
            auto path=QFileDialog::getOpenFileName(this,"Select source",sky?QDir(game->text()).filePath("rsrc/world/skybox"):archive->text());
            if(!path.isEmpty()){
                QString label=QFileInfo(path).fileName();
                if(!sky){const SceneIdentity identity=identifyScene(path);
                    if(!identity.title.isEmpty())label=identity.title+" ["+identity.id+"] — "+label;}
                auto item=new QListWidgetItem(label,target);item->setData(Qt::UserRole,path);target->setCurrentItem(item);
            }
        });return page;
    }
    void addModelNode(QTreeWidgetItem *parent,const QJsonObject &part) {
        auto item=new QTreeWidgetItem(parent);
        item->setText(0,part.value("label").toString());
        item->setText(1,part.value("kind").toString());
        if(part.contains("vertices"))item->setText(2,QString::number(part.value("vertices").toInt()));
        for(const QJsonValue &child:part.value("children").toArray())addModelNode(item,child.toObject());
    }
    void showModelInfo(const QJsonObject &info) {
        modelHierarchy->clear();modelProperties->clear();modelChoiceEditors.clear();
        while(auto item=modelChoices->takeAt(0)){if(item->widget())item->widget()->deleteLater();delete item;}
        modelSummary->setText(QString("%1  ·  %2 bytes\n%3")
            .arg(info.value("implementation").toString()).arg(info.value("bytes").toInteger())
            .arg(info.value("relativePath").toString()));
        for(const QJsonValue &part:info.value("hierarchy").toArray()){
            auto root=new QTreeWidgetItem(modelHierarchy);
            const QJsonObject object=part.toObject();
            root->setText(0,object.value("label").toString());root->setText(1,object.value("kind").toString());
            if(object.contains("vertices"))root->setText(2,QString::number(object.value("vertices").toInt()));
            for(const QJsonValue &child:object.value("children").toArray())addModelNode(root,child.toObject());
        }
        modelHierarchy->expandToDepth(1);
        modelVariant->clear();modelVariant->addItem("Model default",QString());
        const QString selected=info.value("selectedVariant").toString();
        int selectedIndex=0;
        for(const QJsonValue &value:info.value("variants").toArray()){
            const QString variant=value.toString();modelVariant->addItem(variant,variant);
            if(variant==selected)selectedIndex=modelVariant->count()-1;
        }
        if(modelVariant->count()>1&&selectedIndex==0&&!selected.isEmpty())selectedIndex=1;
        modelVariant->setCurrentIndex(selectedIndex);
        modelVariant->setEnabled(modelVariant->count()>1);
        for(const QJsonValue &value:info.value("parameters").toArray()){
            const QJsonObject parameter=value.toObject();
            const QString name=parameter.value("name").toString(),kind=parameter.value("kind").toString();
            const QJsonArray options=parameter.value("options").toArray();
            auto row=new QTreeWidgetItem(modelProperties,{name,kind,QString::number(options.size())});
            for(const QJsonValue &choice:options)new QTreeWidgetItem(row,{choice.toString(),"option",""});
            if(kind=="Choice"&&name!="Model"&&!options.isEmpty()){
                auto editor=new QComboBox;editor->addItem("Model default",QString());
                for(const QJsonValue &choice:options)editor->addItem(choice.toString(),choice.toString());
                modelChoices->addRow(name,editor);modelChoiceEditors.insert(name,editor);
            }
        }
    }
    void inspectModel(const QString &source) {
        if(inspectProcess){inspectProcess->kill();inspectProcess->deleteLater();inspectProcess=nullptr;}
        modelHierarchy->clear();modelProperties->clear();modelVariant->clear();modelVariant->setEnabled(false);
        modelChoiceEditors.clear();
        while(auto item=modelChoices->takeAt(0)){if(item->widget())item->widget()->deleteLater();delete item;}
        modelSummary->setText(source.isEmpty()?"Choose a model .dat file.":"Reading model structure…");
        if(source.isEmpty())return;
        const QString runtime=javaExecutable();
        if(!QFileInfo(runtime).isFile()||!QFileInfo(backend("trs-export.jar")).isFile()){
            modelSummary->setText("Packaged Java runtime or model inspector is missing.");return;
        }
        auto inspect=new QProcess(this);inspectProcess=inspect;
        inspect->setProcessChannelMode(QProcess::MergedChannels);
        inspect->setProgram(runtime);
        inspect->setArguments({"-Xshare:off","-cp",backend("trs-export.jar")+QDir::listSeparator()+QDir(game->text()).filePath("code/*"),
            "ModelInspect",QDir(game->text()).filePath("rsrc"),source});
        connect(inspect,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,inspect,source](int code,QProcess::ExitStatus state){
            const QByteArray output=inspect->readAllStandardOutput();
            if(inspectProcess!=inspect){inspect->deleteLater();return;}
            inspectProcess=nullptr;inspect->deleteLater();
            const QByteArray marker="TRS_MODEL_INSPECT:";
            const int start=output.lastIndexOf(marker);
            if(code||state!=QProcess::NormalExit||start<0){modelSummary->setText("Could not inspect this model. "+QString::fromLocal8Bit(output.right(240)).trimmed());return;}
            const QByteArray json=output.mid(start+marker.size()).split('\n').first().trimmed();
            QJsonParseError error;const QJsonDocument document=QJsonDocument::fromJson(json,&error);
            if(error.error!=QJsonParseError::NoError){modelSummary->setText("Model inspection returned invalid data: "+error.errorString());return;}
            if(selection(models)==source)showModelInfo(document.object());
        });
        inspect->start();
    }
    void clearPreviewWindow() {
#ifdef Q_OS_WIN
        if(previewThumbnail){DwmUnregisterThumbnail(previewThumbnail);previewThumbnail=nullptr;}
        previewSourceWindow=nullptr;
#endif
        if(previewOverlay){previewOverlay->hide();previewOverlay->deleteLater();previewOverlay=nullptr;}
        if(previewPlaceholder)previewPlaceholder->show();
    }
    void updatePreviewThumbnail() {
#ifdef Q_OS_WIN
        if(!previewThumbnail||!previewHost||!previewOverlay)return;
        const bool visible=previewHost->isVisible()&&isVisible()&&!isMinimized()&&tabs&&tabs->currentIndex()==1;
        previewOverlay->setVisible(visible);if(!visible)return;
        const QPoint global=previewHost->mapToGlobal(QPoint(0,0));
        previewOverlay->setGeometry(global.x(),global.y(),previewHost->width(),previewHost->height());
        const qreal scale=devicePixelRatioF();
        DWM_THUMBNAIL_PROPERTIES properties{};properties.dwFlags=DWM_TNP_RECTDESTINATION|DWM_TNP_VISIBLE|DWM_TNP_OPACITY|DWM_TNP_SOURCECLIENTAREAONLY;
        properties.rcDestination={0,0,LONG(previewHost->width()*scale),LONG(previewHost->height()*scale)};
        properties.fVisible=TRUE;properties.opacity=255;properties.fSourceClientAreaOnly=TRUE;
        DwmUpdateThumbnailProperties(previewThumbnail,&properties);
#endif
    }
    void attachPreviewWindow(int attempts=0) {
        if(!previewProcess||previewProcess->state()==QProcess::NotRunning)return;
#ifdef Q_OS_WIN
        if(previewThumbnail)return;
        PreviewWindowSearch search{DWORD(previewProcess->processId())};
        EnumWindows(findPreviewWindow,reinterpret_cast<LPARAM>(&search));
        if(search.result&&search.area>=120000){
            previewSourceWindow=search.result;
            previewOverlay=new QWidget(this,Qt::Tool|Qt::FramelessWindowHint);
            previewOverlay->setAttribute(Qt::WA_ShowWithoutActivating);previewOverlay->setStyleSheet("background:#202020;");
            previewOverlay->show();
            if(SUCCEEDED(DwmRegisterThumbnail(HWND(previewOverlay->winId()),previewSourceWindow,&previewThumbnail))){
                SetWindowPos(previewSourceWindow,HWND_BOTTOM,0,0,900,700,SWP_NOACTIVATE|SWP_SHOWWINDOW);
                previewPlaceholder->hide();updatePreviewThumbnail();
                status->setText("SpiralView is rendering in a separate process; Windows is compositing its live preview here.");
                return;
            }
            previewOverlay->deleteLater();previewOverlay=nullptr;
        }
#endif
        if(attempts<40)QTimer::singleShot(250,this,[this,attempts]{attachPreviewWindow(attempts+1);});
        else status->setText("SpiralView started but its preview window could not be attached. Check the preview log.");
    }
    void stopPreview() {
        if(!previewProcess){clearPreviewWindow();return;}
        status->setText("Closing SpiralView preview…");
        previewProcess->terminate();
        QTimer::singleShot(1500,previewProcess,[process=previewProcess]{if(process->state()!=QProcess::NotRunning)process->kill();});
    }
    void previewModel() {
        const QString source=selection(models),runtime=javaExecutable();
        if(source.isEmpty()){status->setText("Choose a model to preview.");return;}
        const QString viewer=backend("spiralview.jar");
        if(!QFileInfo(viewer).isFile()||!QFileInfo(runtime).isFile()){
            status->setText("The packaged SpiralView or a compatible Java runtime is missing.");return;
        }
        if(output->text().trimmed().isEmpty()){
            status->setText("Choose an output folder for the SpiralView startup log.");return;
        }
        if(previewProcess){status->setText("Close the current SpiralView preview before opening another model.");return;}
        const QString code=QDir(game->text()).filePath("code");
        const QStringList jars={"projectx-config.jar","projectx-pcode.jar","lwjgl.jar","lwjgl-opengl.jar","lwjgl-openal.jar",
            "lwjgl-glfw.jar","lwjgl-jawt.jar","lwjgl3-awt.jar","lwjgl-natives-windows.jar","lwjgl-opengl-natives-windows.jar",
            "lwjgl-openal-natives-windows.jar","lwjgl-glfw-natives-windows.jar","commons-beanutils.jar",
            "commons-digester.jar","commons-logging.jar"};
        QStringList classpath{viewer},missing;
        for(const QString &name:jars){const QString path=QDir(code).filePath(name);classpath<<path;if(!QFileInfo(path).isFile())missing<<name;}
        if(!missing.isEmpty()){status->setText("The game installation is missing SpiralView dependencies: "+missing.join(", "));return;}
        QString modelArgument=QDir(game->text()).relativeFilePath(source);
        if(modelArgument.startsWith("../"))modelArgument=source;
        modelArgument=QDir::fromNativeSeparators(modelArgument);
        QStringList args={"-Xms512m","-Xmx2G","-Xshare:off","-Dcom.threerings.getdown=false","-Dorg.lwjgl.util.NoChecks=true",
            "-Dsun.java2d.d3d=false","-XX:-CreateCoredumpOnCrash","-XX:+SuppressFatalErrorMessage",
            "--add-opens=java.base/java.lang=ALL-UNNAMED","--add-opens=java.base/java.util=ALL-UNNAMED",
            "--enable-native-access=ALL-UNNAMED","-Dappdir="+QDir::fromNativeSeparators(game->text())+"/",
            "-Dresource_dir="+QDir(game->text()).filePath("rsrc"),"-Djava.library.path="+QDir(game->text()).filePath("native"),
            "-classpath",classpath.join(QDir::listSeparator()),"com.lucasluqui.spiralview.ModelViewerHook",modelArgument};
        QDir().mkpath(output->text());
        const QString previewLog=QDir(output->text()).filePath(prefixFor("model",source,game->text())+".spiralview.log");
        auto viewerProcess=new QProcess(this);previewProcess=viewerProcess;
        viewerProcess->setWorkingDirectory(game->text());viewerProcess->setProcessChannelMode(QProcess::MergedChannels);
        viewerProcess->setStandardOutputFile(previewLog,QIODevice::Truncate);
        connect(viewerProcess,&QProcess::started,this,[this]{QTimer::singleShot(750,this,[this]{attachPreviewWindow();});});
        connect(viewerProcess,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this,viewerProcess,previewLog](int code,QProcess::ExitStatus){
            if(previewProcess==viewerProcess)previewProcess=nullptr;
            clearPreviewWindow();viewerProcess->deleteLater();
            status->setText(code?"SpiralView exited with an error. See "+previewLog+" and the game's viewer.log.":"SpiralView preview closed.");
        });
        connect(viewerProcess,&QProcess::errorOccurred,this,[this,viewerProcess,previewLog](QProcess::ProcessError error){
            if(error==QProcess::FailedToStart){if(previewProcess==viewerProcess)previewProcess=nullptr;clearPreviewWindow();status->setText("Could not start SpiralView. See "+previewLog);viewerProcess->deleteLater();}
        });
        viewerProcess->start(runtime,args);
        status->setText("Launching the current KnightLauncher SpiralView…");
    }
    QWidget *modelPage() {
        auto page=new QWidget;auto row=new QHBoxLayout(page);
        auto left=new QWidget;auto leftLayout=new QVBoxLayout(left);
        auto filter=new QLineEdit;filter->setPlaceholderText("Filter model paths…");leftLayout->addWidget(filter);
        models=new QListWidget;leftLayout->addWidget(models,1);
        auto scan=new QPushButton("Scan game models");leftLayout->addWidget(scan);
        auto choose=new QPushButton("Choose model .dat…");leftLayout->addWidget(choose);
        row->addWidget(left,1);
        auto right=new QWidget;auto details=new QVBoxLayout(right);
        modelSummary=new QLabel("Choose a model .dat file.");modelSummary->setWordWrap(true);details->addWidget(modelSummary);
        auto previewButtons=new QHBoxLayout;
        auto preview=new QPushButton("Preview in SpiralView");previewButtons->addWidget(preview);
        auto closePreview=new QPushButton("Close preview");previewButtons->addWidget(closePreview);
        details->addLayout(previewButtons);
        auto blenderPreview=new QPushButton("Preview in Blender");details->addWidget(blenderPreview);
        previewHost=new QWidget;previewHost->setMinimumHeight(300);previewHost->setSizePolicy(QSizePolicy::Expanding,QSizePolicy::Expanding);
        auto previewLayout=new QVBoxLayout(previewHost);previewLayout->setContentsMargins(0,0,0,0);
        previewPlaceholder=new QLabel("SpiralView preview appears here in its own process.\nSelect a model, then click Preview in SpiralView.");
        previewPlaceholder->setAlignment(Qt::AlignCenter);previewPlaceholder->setWordWrap(true);
        previewPlaceholder->setStyleSheet("QLabel{border:1px solid palette(mid);border-radius:4px;background:palette(base);color:palette(text);}");
        previewLayout->addWidget(previewPlaceholder);details->addWidget(previewHost,3);
        auto variantRow=new QFormLayout;modelVariant=new QComboBox;modelVariant->setEnabled(false);variantRow->addRow("Static-set variant",modelVariant);details->addLayout(variantRow);
        glb=new QCheckBox("Also export a single .glb model");glb->setChecked(true);details->addWidget(glb);
        auto choicesBox=new QGroupBox("Choice parameters");modelChoices=new QFormLayout(choicesBox);details->addWidget(choicesBox);
        details->addWidget(new QLabel("Model hierarchy"));modelHierarchy=new QTreeWidget;modelHierarchy->setHeaderLabels({"Part","Kind","Vertices"});details->addWidget(modelHierarchy,1);
        details->addWidget(new QLabel("Source parameters"));modelProperties=new QTreeWidget;modelProperties->setHeaderLabels({"Parameter","Kind","Options"});details->addWidget(modelProperties,1);
        row->addWidget(right,2);
        connect(scan,&QPushButton::clicked,this,[this]{
            models->clear();const QString root=QDir(game->text()).filePath("rsrc");QDirIterator it(root,{"*.dat"},QDir::Files,QDirIterator::Subdirectories);
            while(it.hasNext()){const QString path=it.next();const QString name=QDir(root).relativeFilePath(path);
                auto item=new QListWidgetItem(name,models);item->setData(Qt::UserRole,path);}
            status->setText(QString("%1 .dat files found. Select a model to inspect.").arg(models->count()));
        });
        connect(filter,&QLineEdit::textChanged,this,[this](const QString &text){for(int i=0;i<models->count();i++)models->item(i)->setHidden(!models->item(i)->text().contains(text,Qt::CaseInsensitive));});
        connect(choose,&QPushButton::clicked,this,[this]{
            const QString path=QFileDialog::getOpenFileName(this,"Select game model",QDir(game->text()).filePath("rsrc"),"Clyde model (*.dat)");
            if(path.isEmpty())return;
            auto item=new QListWidgetItem(QDir(QDir(game->text()).filePath("rsrc")).relativeFilePath(path),models);
            item->setData(Qt::UserRole,path);models->setCurrentItem(item);
        });
        connect(models,&QListWidget::currentItemChanged,this,[this](QListWidgetItem *item){inspectModel(item?item->data(Qt::UserRole).toString():QString());});
        connect(preview,&QPushButton::clicked,this,[this]{previewModel();});
        connect(closePreview,&QPushButton::clicked,this,[this]{stopPreview();});
        connect(blenderPreview,&QPushButton::clicked,this,[this]{
            if(selection(models).isEmpty()){status->setText("Choose a model to preview.");return;}
            openModelInBlender=true;start("model",selection(models));
        });
        return page;
    }
protected:
    void moveEvent(QMoveEvent *event) override {
        QMainWindow::moveEvent(event);QTimer::singleShot(0,this,[this]{updatePreviewThumbnail();});
    }
    void resizeEvent(QResizeEvent *event) override {
        QMainWindow::resizeEvent(event);QTimer::singleShot(0,this,[this]{updatePreviewThumbnail();});
    }
    void changeEvent(QEvent *event) override {
        QMainWindow::changeEvent(event);
        if(event->type()==QEvent::WindowStateChange)QTimer::singleShot(0,this,[this]{updatePreviewThumbnail();});
    }
    void closeEvent(QCloseEvent *event) override {
        if(busy){status->setText("Cancel the running export before closing.");event->ignore();}
        else {if(inspectProcess)inspectProcess->kill();clearPreviewWindow();if(previewProcess)previewProcess->kill();event->accept();}
    }
public:
    Studio() {
        QFile paths(QCoreApplication::applicationDirPath()+"/paths.json");
        if(paths.open(QIODevice::ReadOnly))defaults=QJsonDocument::fromJson(paths.readAll()).object();
        loadSceneCatalog();
        setWindowTitle("ThreeRings Studio · " TRS_VERSION " · Preview");resize(1120,850);
        auto central=new QWidget;setCentralWidget(central);auto layout=new QVBoxLayout(central);
        auto title=new QLabel("ThreeRings Studio");title->setStyleSheet("font-size:25px;font-weight:600;");layout->addWidget(title);
        auto subtitle=new QLabel("Spiral Knights → Blender  •  models, animations, levels and skyboxes");layout->addWidget(subtitle);
        blender=new QLineEdit(this);java=new QLineEdit(this);
        globals=new QLineEdit(backend("scene_global.xml"),this);
        colorNode=new QLineEdit(this);
        blender->hide();java->hide();globals->hide();colorNode->hide();
        const QString bundledNode=backend("Color_Replacement_Node.blend");
        if(QFileInfo(bundledNode).isFile())colorNode->setText(bundledNode);
        auto config=new QGroupBox("Folders");auto form=new QFormLayout(config);
        game=pathRow(form,"Spiral Knights installation","game",true);
        output=pathRow(form,"Output folder (reused)","output",true);
        archive=pathRow(form,"Scenes / levels folder","archive",true);
        output->setPlaceholderText("Choose one folder for all named level, skybox and animation exports");
        layout->addWidget(config);
        tabs=new QTabWidget;
        auto animations=new QWidget;auto animLayout=new QVBoxLayout(animations);
        auto explanation=new QLabel("Export stored knight clips as Blender Actions. Select a rigged UE character .blend below to also bake those Actions onto its existing skeleton and meshes.");
        explanation->setWordWrap(true);animLayout->addWidget(explanation);
        ueNames=new QCheckBox("Use anatomical UE bone names");ueNames->setChecked(true);animLayout->addWidget(ueNames);
        fbx=new QCheckBox("Also export FBX clips (UE skeletal mesh and animations when a character is selected)");animLayout->addWidget(fbx);
        auto ueForm=new QFormLayout;
        ueCharacter=pathRow(ueForm,"Rigged UE character .blend","ue-character",false);
        ueCharacter->setPlaceholderText("Optional: retarget onto this model's existing 88-bone armature");
        animLayout->addLayout(ueForm);
        connect(ueCharacter,&QLineEdit::textChanged,this,[this](const QString &path){
            const bool retarget=!path.trimmed().isEmpty();
            if(retarget)ueNames->setChecked(true);
            ueNames->setEnabled(!retarget);
        });
        if(!ueCharacter->text().trimmed().isEmpty()){ueNames->setChecked(true);ueNames->setEnabled(false);}
        animLayout->addStretch();tabs->addTab(animations,"Knight animations");
        tabs->addTab(modelPage(),"Models");
        tabs->addTab(sourcePage("Select a saved SKSA scene. Static geometry, instances and textures are exported; runtime-only effects are listed in the report.",levels,false),"Levels");
        tabs->addTab(sourcePage("Select a skybox wrapper or model. Geometry and parallax metadata are preserved. Camera-follow motion is not baked.",skies,true),"Skyboxes");
        connect(tabs,&QTabWidget::currentChanged,this,[this]{QTimer::singleShot(0,this,[this]{updatePreviewThumbnail();});});
        layout->addWidget(tabs,1);
        auto actions=new QHBoxLayout;run=new QPushButton("Export to Blender");run->setDefault(true);actions->addWidget(run);
        cancel=new QPushButton("Cancel");cancel->setEnabled(false);actions->addWidget(cancel);
        openResult=new QPushButton("Open output folder");openResult->setEnabled(false);actions->addWidget(openResult);
        actions->addStretch();layout->addLayout(actions);
        status=new QLabel("Ready. Configure paths and choose an export.");status->setWordWrap(true);layout->addWidget(status);
        progress=new QProgressBar;progress->setRange(0,1);progress->setValue(0);layout->addWidget(progress);
        log=new QPlainTextEdit;log->setReadOnly(true);log->setMaximumBlockCount(10000);log->setMinimumHeight(120);layout->addWidget(log);
        connect(run,&QPushButton::clicked,this,[this]{
            const int tab=tabs->currentIndex();
            start(tab==0?"animation":tab==1?"model":tab==2?"scene":"skybox",
                tab==1?selection(models):tab==2?selection(levels):tab==3?selection(skies):QString());
        });
        connect(cancel,&QPushButton::clicked,this,[this]{cancelled=true;cancel->setEnabled(false);process.kill();});
        connect(openResult,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(resultPath).absolutePath()));});
        process.setProcessChannelMode(QProcess::MergedChannels);
        connect(&process,&QProcess::readyReadStandardOutput,this,[this]{append(QString::fromLocal8Bit(process.readAllStandardOutput()));});
        connect(&process,&QProcess::errorOccurred,this,[this](QProcess::ProcessError e){if(e==QProcess::FailedToStart)finish(false,"Could not start backend: "+process.errorString());});
        connect(&process,qOverload<int,QProcess::ExitStatus>(&QProcess::finished),this,[this](int code,QProcess::ExitStatus state){
            append(QString::fromLocal8Bit(process.readAllStandardOutput()));
            if(cancelled){next();return;}
            if(state!=QProcess::NormalExit||code<0||code>active.maximumExit){finish(false,QString("Failed during %1 (exit %2). See the log and retained partial output.").arg(active.title).arg(code));return;}
            next();
        });
    }
    void start(const QString &kind,const QString &source) {
        if(busy)return;
        busy=true;cancelled=false;mode=kind;currentSource=source;log->clear();pending.clear();selectedOutput.clear();stagingPath.clear();ueResultPath.clear();
        auto fail=[this](const QString &s){finish(false,s);};
        if(game->text().trimmed().isEmpty()){fail("Choose the Spiral Knights installation folder.");return;}
        if(output->text().trimmed().isEmpty()){fail("Choose an export folder.");return;}
        QString destination=QFileInfo(output->text()).absoluteFilePath();
        if(!QDir().mkpath(destination)){fail("Cannot create the selected export folder.");return;}
        selectedOutput=destination;
        currentScene=kind=="scene"?identifyScene(source):SceneIdentity{};
        previousPrefix=prefixFor(kind,source,game->text());
        QString displayTitle=currentScene.title;
        if(kind=="model"){
            displayTitle=QFileInfo(source).dir().dirName()+"-"+QFileInfo(source).completeBaseName();
            const QString variant=batchModelVariant.isEmpty()?(modelVariant->currentIndex()>0?modelVariant->currentText():QString()):batchModelVariant;
            if(!variant.isEmpty())displayTitle+="-"+variant;
        }
        exportPrefix=prefixFor(kind,source,game->text(),displayTitle);
        logFile.setFileName(QDir(destination).filePath(exportPrefix+".export.log"));
        if(!logFile.open(QIODevice::WriteOnly|QIODevice::Truncate)){fail("Cannot replace the export log in the selected folder.");return;}
        append("ThreeRings Studio " TRS_VERSION "\nLog: "+logFile.fileName()+"\nMode: "+kind+"\nSource: "+source+"\n");
        if(!currentScene.title.isEmpty())append("Recognized scene: "+currentScene.title+" ["+currentScene.id+"] via "+currentScene.method+"\n");
        QFile identity(QCoreApplication::applicationDirPath()+"/build-identity.json");
        if(identity.open(QIODevice::ReadOnly))append(QString::fromUtf8(identity.readAll())+"\n");
        QString rsrc=QDir(game->text()).filePath("rsrc");
        if(!QFileInfo::exists(rsrc+"/character/pc/model.dat")){fail("Game resource directory is incomplete.");return;}
        if(!QFileInfo(blenderExecutable()).isFile()){fail("Packaged Blender 4.2 LTS is missing; restore the complete application folder.");return;}
        if(kind!="animation"&&!QFileInfo(source).isFile()){fail("Select a readable source file.");return;}
        if(!QFileInfo::exists(backend("trs-export.jar"))||!QFileInfo::exists(backend("build_blend.py"))){fail("Backend files missing. Restore the complete application folder.");return;}
        const QString ueModel=kind=="animation"?ueCharacter->text().trimmed():QString();
        if(!ueModel.isEmpty() && (!QFileInfo(ueModel).isFile() || !ueModel.endsWith(".blend",Qt::CaseInsensitive))){
            fail("Choose a readable rigged UE character .blend file.");return;
        }
        stagingPath=QDir(destination).filePath(".trs-stage-"+QUuid::createUuid().toString(QUuid::WithoutBraces));
        QString javaPath=javaExecutable();
        if(java->text().isEmpty()) {
            QString runtime=QDir(game->text()).filePath("java_vm");
            const QString installedJava=runtime+"/bin/java.exe";
            if(QFileInfo::exists(runtime+"/lib/ext")) {
                QString cache=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/game-java";
                // The cache belongs exclusively to TRS. Remove a stale extension folder before
                // the incremental copy because modern Java refuses to start when it is present.
                QDir(cache+"/lib/ext").removeRecursively();
                pending.append({"Prepare compatible game Java runtime","C:/Windows/System32/robocopy.exe",{QDir::toNativeSeparators(runtime),QDir::toNativeSeparators(cache),"/E","/XD","ext","/R:1","/W:1","/NFL","/NDL","/NJH","/NJS","/NP"},7});
                javaPath=cache+"/bin/java.exe";
            }else if(QFileInfo::exists(installedJava))javaPath=installedJava;
        }
        if(pending.isEmpty()&&!QFileInfo(javaPath).isFile()){fail("Java executable missing; choose a runtime compatible with the installed game.");return;}
        QStringList args={"-Xmx4g","-Xshare:off","-cp",backend("trs-export.jar")+QDir::listSeparator()+QDir(game->text()).filePath("code/*"),kind=="animation"?"KnightAnimationExport":"SceneExport",rsrc};
        if(kind!="animation")args<<source;
        args<<stagingPath;
        if(kind!="animation"&&!globals->text().isEmpty()){
            if(!QFileInfo(globals->text()).isFile()){fail("Legacy globals XML path does not exist. Select it or clear the field.");return;}
            args<<globals->text();
        }
        if(kind=="scene"){
            if(globals->text().isEmpty())args<<"";
            args<<currentScene.title<<currentScene.id<<currentScene.method;
        }else if(kind=="model"){
            if(globals->text().isEmpty())args<<"";
            const QString variant=batchModelVariant.isEmpty()?(modelVariant->currentIndex()>0?modelVariant->currentData().toString():QString()):batchModelVariant;
            if(!variant.isEmpty())args<<"Model="+variant;
            for(auto it=modelChoiceEditors.cbegin();it!=modelChoiceEditors.cend();++it)
                if(it.value()->currentIndex()>0)args<<it.key()+"="+it.value()->currentData().toString();
        }
        pending.append({"Extract game data",javaPath,args});
        bundlePath=stagingPath+(kind=="animation"?"/knight-animations.json":"/scene.json");resultPath=stagingPath+"/"+(kind=="animation"?"Knight-Animations.blend":kind=="scene"?"Level.blend":kind=="model"?"Model.blend":"Skybox.blend");
        QString logicalBundle=QDir(destination).filePath(exportPrefix+"."+(kind=="animation"?"knight-animations.json":"scene.json"));
        QStringList blendArgs={"--background","--factory-startup","--python-exit-code","1","--python",backend("build_blend.py"),"--",kind,bundlePath,resultPath,"--logical-bundle",logicalBundle};
        if(kind=="animation"&&(ueNames->isChecked()||!ueModel.isEmpty()))blendArgs<<"--ue-names";
        if(kind=="animation"&&fbx->isChecked()&&ueModel.isEmpty())blendArgs<<"--fbx";
        if(kind=="model"&&glb->isChecked())blendArgs<<"--glb";
        if(!colorNode->text().isEmpty()){
            if(!QFileInfo(colorNode->text()).isFile()){fail("Color node library does not exist.");return;}
            blendArgs<<"--color-node"<<colorNode->text();
        }
        pending.append({"Build Blender file",blenderExecutable(),blendArgs});
        if(!ueModel.isEmpty()){
            if(!QFileInfo(backend("retarget_ue.py")).isFile()){
                fail("UE retarget script is missing from the package.");return;
            }
            ueResultPath=stagingPath+"/UE-Retargeted.blend";
            QStringList retargetArgs={"--background",ueModel,"--python-exit-code","1","--python",backend("retarget_ue.py"),"--",
                                      resultPath,ueResultPath,stagingPath+"/ue-retarget-report.json"};
            if(fbx->isChecked()){
                retargetArgs<<"--fbx-directory"<<stagingPath+"/ue-fbx";
                if(!batchUeFbxClips.isEmpty())retargetArgs<<"--fbx-clips"<<batchUeFbxClips;
            }
            pending.append({"Retarget onto UE character",blenderExecutable(),retargetArgs});
        }
        append("Output: "+destination+"\n");run->setEnabled(false);cancel->setEnabled(true);openResult->setEnabled(false);tabs->setEnabled(false);progress->setRange(0,0);next();
    }
    void commandLine(const QCommandLineParser &parser) {
        for(auto pair:QList<QPair<QString,QLineEdit*>>{{"game",game},{"blender",blender},{"java",java},{"output",output},{"globals",globals},{"archive",archive},{"color-node",colorNode},{"ue-character",ueCharacter}})
            if(parser.isSet(pair.first))pair.second->setText(parser.value(pair.first));
        ueNames->setChecked(!parser.isSet("source-names"));fbx->setChecked(parser.isSet("fbx"));
        batchModelVariant=parser.value("model-variant");
        batchUeFbxClips=parser.value("ue-fbx-clips");
        if(parser.isSet("tab")){
            const QString tab=parser.value("tab");
            tabs->setCurrentIndex(tab=="models"?1:tab=="levels"?2:tab=="skyboxes"?3:0);
        }
        if(parser.isSet("preview-source")){
            const QString source=QFileInfo(parser.value("preview-source")).absoluteFilePath();
            auto item=new QListWidgetItem(QDir(QDir(game->text()).filePath("rsrc")).relativeFilePath(source),models);
            item->setData(Qt::UserRole,source);models->setCurrentItem(item);tabs->setCurrentIndex(1);
            QTimer::singleShot(500,this,[this]{previewModel();});
        }
        if(parser.isSet("batch")){
            batch=true;QString kind=parser.value("batch");
            tabs->setCurrentIndex(kind=="model"?1:kind=="scene"?2:kind=="skybox"?3:0);
            QTimer::singleShot(0,this,[this,kind,&parser]{
                if(kind!="animation"&&kind!="scene"&&kind!="skybox"&&kind!="model"){QCoreApplication::exit(2);return;}
                start(kind,parser.value("source"));
            });
            if(parser.isSet("cancel-after-ms"))QTimer::singleShot(qMax(1,parser.value("cancel-after-ms").toInt()),this,[this]{cancel->click();});
        }
        if(parser.isSet("screenshot"))QTimer::singleShot(parser.isSet("preview-source")?7000:400,this,[this,&parser]{
            const QRect bounds=frameGeometry();
            const QPixmap image=parser.isSet("preview-source")?screen()->grabWindow(0,bounds.x(),bounds.y(),bounds.width(),bounds.height()):grab();
            image.save(parser.value("screenshot"));if(!batch)qApp->quit();
        });
    }
};

int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setApplicationName("ThreeRingsStudio");app.setOrganizationName("ThreeRingsSharp");app.setApplicationVersion(TRS_VERSION);
    QCommandLineParser parser;parser.setApplicationDescription("ThreeRings Studio export frontend");parser.addHelpOption();parser.addVersionOption();
    for(const auto &name:{"batch","source","game","blender","java","output","globals","archive","screenshot","color-node","cancel-after-ms","model-variant","tab","ue-character","ue-fbx-clips","preview-source"})parser.addOption({name,QString("Set %1").arg(name),"value"});
    parser.addOption({"source-names","Keep original SK bone names"});parser.addOption({"fbx","Also export animation FBX files"});
    parser.addOption({"glb","Also export a single GLB for a selected model (enabled by default)"});parser.process(app);
    Studio window;if(!parser.isSet("batch")||parser.isSet("screenshot"))window.show();window.commandLine(parser);return app.exec();
}
