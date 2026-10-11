#include <QtWidgets>
#include <QProcess>
#include <QSaveFile>
#include <functional>

struct Step { QString title, program; QStringList arguments; int maximumExit = 0; };

// Process boundaries isolate old IKVM/.NET and current Java formats from Qt.
// No parsing/exporting code from the working legacy backend is modified.
class Studio : public QMainWindow {
    QLineEdit *game, *blender, *java, *archive, *output, *globals, *legacy, *colorNode;
    QTabWidget *tabs;
    QListWidget *levels, *skies;
    QPlainTextEdit *log;
    QLabel *status;
    QPushButton *run, *cancel, *openResult;
    QCheckBox *ueNames, *fbx;
    QProgressBar *progress;
    QProcess process;
    QList<Step> pending;
    Step active;
    QFile logFile;
    QString resultPath, bundlePath, mode;
    bool batch = false, busy = false, cancelled = false;
    QSettings settings;
    QJsonObject defaults;

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
                if(key=="output")selected=QDir(selected).filePath("TRS-Export-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss"));
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
    void finish(bool success, const QString &message) {
        if(!busy) return;
        append("\n"+message+"\n"); pending.clear(); busy=false;
        status->setText(message); run->setEnabled(true); cancel->setEnabled(false);
        tabs->setEnabled(true); progress->setRange(0,1); progress->setValue(success?1:0);
        openResult->setEnabled(success); logFile.close();
        if(batch) QTimer::singleShot(0,qApp,[success]{QCoreApplication::exit(success?0:1);});
    }
    void next() {
        if(cancelled){finish(false,"Cancelled. Partial files are retained; use a new output folder to retry.");return;}
        if(pending.isEmpty()) {
            if(!QFileInfo::exists(resultPath)){finish(false,"Backend exited without the expected .blend file.");return;}
            QFile report(QFileInfo(bundlePath).dir().filePath(mode=="animation"?"export-report.json":"scene-report.json"));
            int warnings = 0;
            if(report.open(QIODevice::ReadOnly)) {
                auto doc=QJsonDocument::fromJson(report.readAll()).object();
                warnings=doc.value(mode=="animation"?"skipped":"issues").toArray().size();
            }
            finish(true,QString("Export complete — %1 reported limitations. %2").arg(warnings).arg(resultPath));return;
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
            auto item=new QListWidgetItem(root.relativeFilePath(path),list);item->setData(Qt::UserRole,path);
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
            if(!path.isEmpty()){auto item=new QListWidgetItem(QFileInfo(path).fileName(),target);item->setData(Qt::UserRole,path);target->setCurrentItem(item);}
        });return page;
    }
protected:
    void closeEvent(QCloseEvent *event) override {
        if(busy){status->setText("Cancel the running export before closing.");event->ignore();}
        else event->accept();
    }
public:
    Studio() {
        QFile paths(QCoreApplication::applicationDirPath()+"/paths.json");
        if(paths.open(QIODevice::ReadOnly))defaults=QJsonDocument::fromJson(paths.readAll()).object();
        setWindowTitle("ThreeRings Studio · " TRS_VERSION " · Preview");resize(1120,850);
        auto central=new QWidget;setCentralWidget(central);auto layout=new QVBoxLayout(central);
        auto title=new QLabel("ThreeRings Studio");title->setStyleSheet("font-size:25px;font-weight:600;");layout->addWidget(title);
        auto subtitle=new QLabel("Spiral Knights → Blender  •  animations, scenes and skyboxes");layout->addWidget(subtitle);
        auto config=new QGroupBox("Paths");auto form=new QFormLayout(config);
        game=pathRow(form,"Game installation","game",true,"E:/SteamLibrary/steamapps/common/Spiral Knights");
        blender=pathRow(form,"Blender 4.2 LTS executable","blender",false);
        java=pathRow(form,"Java executable (optional)","java",false);
        archive=pathRow(form,"Level archive folder","archive",true);
        globals=pathRow(form,"Legacy scene globals XML","globals",false,backend("scene_global.xml"));
        legacy=pathRow(form,"Original SKAnimatorTools.exe","legacy",false);
        colorNode=pathRow(form,"Optional color node .blend","color-node",false);
        output=pathRow(form,"New export folder","output",true);
        output->setPlaceholderText("Enter a new folder; existing folders are never overwritten");
        layout->addWidget(config);
        tabs=new QTabWidget;
        auto animations=new QWidget;auto animLayout=new QVBoxLayout(animations);
        auto explanation=new QLabel("Export all stored knight animation clips as Blender Actions.\nUE names follow anatomy and retain the source skeleton; this does not retarget to the UE mannequin.");
        explanation->setWordWrap(true);animLayout->addWidget(explanation);
        ueNames=new QCheckBox("Use anatomical UE bone names");ueNames->setChecked(true);animLayout->addWidget(ueNames);
        fbx=new QCheckBox("Also export one FBX per clip (experimental weapon helper transforms)");animLayout->addWidget(fbx);
        animLayout->addStretch();tabs->addTab(animations,"Knight animations");
        tabs->addTab(sourcePage("Select a saved SKSA scene. Static geometry, instances and textures are exported; runtime-only effects are listed in the report.",levels,false),"Levels");
        tabs->addTab(sourcePage("Select a skybox wrapper or model. Geometry and parallax metadata are preserved. Camera-follow motion is not baked.",skies,true),"Skyboxes");
        layout->addWidget(tabs,1);
        auto actions=new QHBoxLayout;run=new QPushButton("Export to Blender");run->setDefault(true);actions->addWidget(run);
        cancel=new QPushButton("Cancel");cancel->setEnabled(false);actions->addWidget(cancel);
        openResult=new QPushButton("Open output folder");openResult->setEnabled(false);actions->addWidget(openResult);
        auto old=new QPushButton("Open original model exporter");actions->addWidget(old);actions->addStretch();layout->addLayout(actions);
        status=new QLabel("Ready. Configure paths and choose an export.");status->setWordWrap(true);layout->addWidget(status);
        progress=new QProgressBar;progress->setRange(0,1);progress->setValue(0);layout->addWidget(progress);
        log=new QPlainTextEdit;log->setReadOnly(true);log->setMaximumBlockCount(10000);log->setMinimumHeight(120);layout->addWidget(log);
        connect(run,&QPushButton::clicked,this,[this]{start(tabs->currentIndex()==0?"animation":tabs->currentIndex()==1?"scene":"skybox",tabs->currentIndex()==1?selection(levels):selection(skies));});
        connect(cancel,&QPushButton::clicked,this,[this]{cancelled=true;cancel->setEnabled(false);process.kill();});
        connect(openResult,&QPushButton::clicked,this,[this]{QDesktopServices::openUrl(QUrl::fromLocalFile(QFileInfo(resultPath).absolutePath()));});
        connect(old,&QPushButton::clicked,this,[this]{
            if(!QFileInfo::exists(legacy->text())||!QProcess::startDetached(legacy->text(),{},QFileInfo(legacy->text()).absolutePath()))status->setText("Choose the original SKAnimatorTools.exe, with its existing DLLs alongside it.");
        });
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
        busy=true;cancelled=false;mode=kind;log->clear();
        QString logs=QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation)+"/logs";QDir().mkpath(logs);
        logFile.setFileName(logs+"/export-"+QDateTime::currentDateTime().toString("yyyyMMdd-HHmmss-zzz")+".log");
        if(!logFile.open(QIODevice::WriteOnly)){finish(false,"Cannot open export log.");return;}
        append("ThreeRings Studio " TRS_VERSION "\nLog: "+logFile.fileName()+"\nMode: "+kind+"\nSource: "+source+"\n");
        QFile identity(QCoreApplication::applicationDirPath()+"/build-identity.json");
        if(identity.open(QIODevice::ReadOnly))append(QString::fromUtf8(identity.readAll())+"\n");
        auto fail=[this](const QString &s){finish(false,s);};
        if(output->text().trimmed().isEmpty()){fail("Enter a new export folder.");return;}
        QString destination=QFileInfo(output->text()).absoluteFilePath();
        if(QFileInfo::exists(destination)){fail("Output folder already exists. Choose a new folder to protect existing work.");return;}
        QString rsrc=QDir(game->text()).filePath("rsrc");
        if(!QFileInfo::exists(rsrc+"/character/pc/model.dat")){fail("Game resource directory is incomplete.");return;}
        if(!QFileInfo(blender->text()).isFile()){fail("Select a Blender 4.2 LTS executable.");return;}
        if(kind!="animation"&&!QFileInfo(source).isFile()){fail("Select a readable source file.");return;}
        if(!QFileInfo::exists(backend("trs-export.jar"))||!QFileInfo::exists(backend("build_blend.py"))){fail("Backend files missing. Restore the complete application folder.");return;}
        QString javaPath=java->text();
        if(javaPath.isEmpty()) {
            QString runtime=QDir(game->text()).filePath("java_vm");javaPath=runtime+"/bin/java.exe";
            if(QFileInfo::exists(runtime+"/lib/ext")) {
                QString cache=QStandardPaths::writableLocation(QStandardPaths::CacheLocation)+"/game-java";
                pending.append({"Prepare compatible game Java runtime","C:/Windows/System32/robocopy.exe",{QDir::toNativeSeparators(runtime),QDir::toNativeSeparators(cache),"/E","/XD","ext","/R:1","/W:1","/NFL","/NDL","/NJH","/NJS","/NP"},7});
                javaPath=cache+"/bin/java.exe";
            }
        }
        if(pending.isEmpty()&&!QFileInfo(javaPath).isFile()){fail("Java executable missing; choose a runtime compatible with the installed game.");return;}
        QStringList args={"-Xmx4g","-Xshare:off","-cp",backend("trs-export.jar")+QDir::listSeparator()+QDir(game->text()).filePath("code/*"),kind=="animation"?"KnightAnimationExport":"SceneExport",rsrc};
        if(kind!="animation")args<<source;
        args<<destination;
        if(kind!="animation"&&!globals->text().isEmpty()){
            if(!QFileInfo(globals->text()).isFile()){fail("Legacy globals XML path does not exist. Select it or clear the field.");return;}
            args<<globals->text();
        }
        pending.append({"Extract game data",javaPath,args});
        bundlePath=destination+(kind=="animation"?"/knight-animations.json":"/scene.json");resultPath=destination+"/"+(kind=="animation"?"Knight-Animations.blend":kind=="scene"?"Level.blend":"Skybox.blend");
        QStringList blendArgs={"--background","--factory-startup","--python-exit-code","1","--python",backend("build_blend.py"),"--",kind,bundlePath,resultPath};
        if(kind=="animation"&&ueNames->isChecked())blendArgs<<"--ue-names";
        if(kind=="animation"&&fbx->isChecked())blendArgs<<"--fbx";
        if(!colorNode->text().isEmpty()){
            if(!QFileInfo(colorNode->text()).isFile()){fail("Color node library does not exist.");return;}
            blendArgs<<"--color-node"<<colorNode->text();
        }
        pending.append({"Build Blender file",blender->text(),blendArgs});
        append("Output: "+destination+"\n");run->setEnabled(false);cancel->setEnabled(true);openResult->setEnabled(false);tabs->setEnabled(false);progress->setRange(0,0);next();
    }
    void commandLine(const QCommandLineParser &parser) {
        for(auto pair:QList<QPair<QString,QLineEdit*>>{{"game",game},{"blender",blender},{"java",java},{"output",output},{"globals",globals},{"archive",archive},{"color-node",colorNode}})
            if(parser.isSet(pair.first))pair.second->setText(parser.value(pair.first));
        ueNames->setChecked(!parser.isSet("source-names"));fbx->setChecked(parser.isSet("fbx"));
        if(parser.isSet("batch")){
            batch=true;QString kind=parser.value("batch");
            QTimer::singleShot(0,this,[this,kind,&parser]{
                if(kind!="animation"&&kind!="scene"&&kind!="skybox"){QCoreApplication::exit(2);return;}
                start(kind,parser.value("source"));
            });
            if(parser.isSet("cancel-after-ms"))QTimer::singleShot(qMax(1,parser.value("cancel-after-ms").toInt()),this,[this]{cancel->click();});
        }
        if(parser.isSet("screenshot"))QTimer::singleShot(400,this,[this,&parser]{grab().save(parser.value("screenshot"));if(!batch)qApp->quit();});
    }
};

int main(int argc,char **argv) {
    QApplication app(argc,argv);app.setApplicationName("ThreeRingsStudio");app.setOrganizationName("ThreeRingsSharp");app.setApplicationVersion(TRS_VERSION);
    QCommandLineParser parser;parser.setApplicationDescription("ThreeRings Studio export frontend");parser.addHelpOption();parser.addVersionOption();
    for(const auto &name:{"batch","source","game","blender","java","output","globals","archive","screenshot","color-node","cancel-after-ms"})parser.addOption({name,QString("Set %1").arg(name),"value"});
    parser.addOption({"source-names","Keep original SK bone names"});parser.addOption({"fbx","Also export animation FBX files"});parser.process(app);
    Studio window;if(!parser.isSet("batch")||parser.isSet("screenshot"))window.show();window.commandLine(parser);return app.exec();
}
