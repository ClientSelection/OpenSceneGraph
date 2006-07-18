/* -*-c++-*- OpenSceneGraph - Copyright (C) 1998-2006 Robert Osfield 
 *
 * This application is open source and may be redistributed and/or modified under  
 * the terms of the GNU Public License (GPL) version 1.0 or 
 * (at your option) any later version.
 * 
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the 
 * OpenSceneGraph Public License for more details.
*/

#include <osg/Notify>
#include <osg/MatrixTransform>
#include <osg/Switch>
#include <osg/PolygonOffset>
#include <osg/CullFace>

#include <osgUtil/Optimizer>

#include <osgDB/FileNameUtils>

#include <osgText/Text>

#include <osgProducer/Viewer>

#include "ImageReaderWriter.h"

using namespace osg;

// now register with Registry to instantiate the above reader/writer,
// declaring in main so that the code to set up PagedLOD can get a handle
// to the ImageReaderWriter's 
osgDB::RegisterReaderWriterProxy<ImageReaderWriter> g_ImageReaderWriter;

class Album;

class Page : public osg::Transform
{
public:


    static Page* createPage(Album* album, unsigned int pageNo, const std::string& frontFileName, const std::string& backFileName, float width, float height)
    {
        osg::ref_ptr<Page> page = new Page(album, pageNo, frontFileName, backFileName, width, height);
        if (page.valid()) return page.release();
        else return 0;
    }
    
    virtual void traverse(osg::NodeVisitor& nv);

    void setRotation(float angle)
    {
        _rotation = angle; 
        _targetRotation = angle; 
        dirtyBound();
    }

    float getRotation() const { return _rotation; }

    void rotateTo(float angle, float timeToRotateBy)
    {
        _targetRotation = angle; 
        _targetTime = timeToRotateBy;
    }
    
    bool rotating() const { return _targetRotation!=_rotation; }

    void setPageVisible(bool frontVisible,bool backVisible)
    {
        _switch->setValue(0,!frontVisible && !backVisible);
        _switch->setValue(1,frontVisible);
        _switch->setValue(2,backVisible);
    }

    osg::Switch* getSwitch() { return _switch.get(); }
    const osg::Switch* getSwitch() const { return _switch.get(); }

public:

    virtual bool computeLocalToWorldMatrix(osg::Matrix& matrix,osg::NodeVisitor*) const 
    {
        if (_referenceFrame==RELATIVE_RF)
        {
            matrix.preMult(getMatrix());
        }
        else // absolute
        {
            matrix = getMatrix();
        }
        return true;
    }

    /** Get the transformation matrix which moves from world coords to local coords.*/
    virtual bool computeWorldToLocalMatrix(osg::Matrix& matrix,osg::NodeVisitor*) const
    {
        const osg::Matrix& inverse = getInverseMatrix();

        if (_referenceFrame==RELATIVE_RF)
        {
            matrix.postMult(inverse);
        }
        else // absolute
        {
            matrix = inverse;
        }
        return true;
    }

    osg::Matrix getMatrix() const { return _pageOffset*osg::Matrix::rotate(-_rotation,0.0f,0.0f,1.0f); }
    osg::Matrix getInverseMatrix() const { return osg::Matrix::inverse(getMatrix()); }

protected:
    
    Page(Album* album, unsigned int pageNo, const std::string& frontFileName, const std::string& backFileName, float width, float height);

    float       _rotation;
    osg::Matrix _pageOffset;

    float       _targetRotation;
    float       _targetTime;
    float       _lastTimeTraverse;

    osg::ref_ptr<osg::Switch>     _switch;

};


class Album : public osg::Referenced
{
public:

    Album(osg::ArgumentParser& ap, float width, float height);

    osg::Group* getScene() { return _group.get(); }
    
    const osg::Group* getScene() const { return _group.get(); }

    osg::Matrix getPageOffset(unsigned int pageNo) const;
    
    bool nextPage(float timeToRotateBy) { return gotoPage(_currentPageNo+1,timeToRotateBy); }

    bool previousPage(float timeToRotateBy) { return _currentPageNo>=1?gotoPage(_currentPageNo-1,timeToRotateBy):false; }
    
    bool gotoPage(unsigned int pageNo, float timeToRotateBy);
    
    osg::StateSet* getBackgroundStateSet() { return _backgroundStateSet.get(); }
    
    void setVisibility();

protected:

    typedef std::vector< osg::ref_ptr<Page> > PageList;

    osg::ref_ptr<osg::Group>    _group;
    PageList                    _pages;
    
    osg::ref_ptr<osg::StateSet> _backgroundStateSet;
    
    unsigned int                _currentPageNo;
    float                       _radiusOfRings;
    float                       _startAngleOfPages;
    float                       _deltaAngleBetweenPages;   

};


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


Page::Page(Album* album, unsigned int pageNo, const std::string& frontFileName, const std::string& backFileName, float width, float height)
{
    // set up transform parts.
    _rotation = 0;
    _targetRotation = 0;
    _targetTime = 0;
    _lastTimeTraverse = 0;
    
    _pageOffset = album->getPageOffset(pageNo);
    
    setNumChildrenRequiringUpdateTraversal(1);
    
    
    // set up subgraph
    osgDB::ReaderWriter* readerWriter = osgDB::Registry::instance()->getReaderWriterForExtension("gdal");
    if (!readerWriter)
    {
        std::cout<<"Error: GDAL plugin not available, cannot preceed with database creation"<<std::endl;
    }

    _switch = new osg::Switch;

    ImageReaderWriter* rw = g_ImageReaderWriter.get();

    
    // set up non visible page.
    osg::Group* non_visible_page = new osg::Group;
    _switch->addChild(non_visible_page);
    {
        osg::Geometry* geom = new osg::Geometry;
        geom->setStateSet(album->getBackgroundStateSet());

        osg::Vec3Array* coords = new osg::Vec3Array(4);
        (*coords)[0].set(0.0f,0.0,height);
        (*coords)[1].set(0.0f,0.0,0);
        (*coords)[2].set(width,0.0,0);
        (*coords)[3].set(width,0.0,height);
        geom->setVertexArray(coords);
        

        osg::Vec3Array* normals = new osg::Vec3Array(4);
        (*normals)[0].set(-1.0f,0.0f,0.0f);
        (*normals)[1].set(0.0f,0.0f,-1.0f);
        (*normals)[2].set(1.0f,0.0f,0.0f);
        (*normals)[3].set(0.0f,0.0f,1.0f);
        geom->setNormalArray(normals);
        geom->setNormalBinding(osg::Geometry::BIND_PER_PRIMITIVE);

        osg::Vec2Array* tcoords = new osg::Vec2Array(4);
        (*tcoords)[0].set(0.0f,1.0f);
        (*tcoords)[1].set(0.0f,0.0f);
        (*tcoords)[2].set(1.0f,0.0f);
        (*tcoords)[3].set(1.0f,1.0f);
        geom->setTexCoordArray(0,tcoords);

        osg::Vec4Array* colours = new osg::Vec4Array(1);
        (*colours)[0].set(1.0f,1.0f,1.0,1.0f);
        geom->setColorArray(colours);
        geom->setColorBinding(osg::Geometry::BIND_OVERALL);

        osg::UByteArray* vindices = new osg::UByteArray(8);
        (*vindices)[0]=0;
        (*vindices)[1]=1;
        (*vindices)[2]=1;
        (*vindices)[3]=2;
        (*vindices)[4]=2;
        (*vindices)[5]=3;
        (*vindices)[6]=3;
        (*vindices)[7]=0;

        geom->setVertexIndices(vindices);
        geom->setTexCoordIndices(0,vindices);

        geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::LINES,0,8));

        // set up the geode.
        osg::Geode* geode = new osg::Geode;
        geode->addDrawable(geom);
        
    
        non_visible_page->addChild(geode);
    }


    // set up visible page.
    osg::Group* front_page = new osg::Group;
    _switch->addChild(front_page);

    {

        osg::Geometry* geom = new osg::Geometry;
        geom->setStateSet(album->getBackgroundStateSet());

        osg::Vec3Array* coords = new osg::Vec3Array(4);
        (*coords)[0].set(0.0f,0.0,height);
        (*coords)[1].set(0.0f,0.0,0);
        (*coords)[2].set(width,0.0,0);
        (*coords)[3].set(width,0.0,height);
        geom->setVertexArray(coords);

        osg::Vec3Array* normals = new osg::Vec3Array(1);
        (*normals)[0].set(0.0f,-1.0f,0.0f);
        geom->setNormalArray(normals);
        geom->setNormalBinding(osg::Geometry::BIND_OVERALL);

        osg::Vec2Array* tcoords = new osg::Vec2Array(4);
        (*tcoords)[0].set(0.0f,1.0f);
        (*tcoords)[1].set(0.0f,0.0f);
        (*tcoords)[2].set(1.0f,0.0f);
        (*tcoords)[3].set(1.0f,1.0f);
        geom->setTexCoordArray(0,tcoords);

        osg::Vec4Array* colours = new osg::Vec4Array(1);
        (*colours)[0].set(1.0f,1.0f,1.0,1.0f);
        geom->setColorArray(colours);
        geom->setColorBinding(osg::Geometry::BIND_OVERALL);

        geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS,0,4));

        // set up the geode.
        osg::Geode* geode = new osg::Geode;
        geode->addDrawable(geom);
        
    
        front_page->addChild(geode);
    }

    if (!frontFileName.empty())
    {
        float cut_off_distance = 8.0f;
        float max_visible_distance = 300.0f;
        
        osg::Vec3 center(width*0.5f,0.0f,height*0.5f);

        osgText::Text* text = new osgText::Text;
        text->setFont("fonts/arial.ttf");
        text->setPosition(center);
        text->setCharacterSize(height/20.0f);
        text->setAlignment(osgText::Text::CENTER_CENTER);
        text->setAxisAlignment(osgText::Text::XZ_PLANE);
        text->setColor(osg::Vec4(1.0f,1.0f,0.0f,1.0f));
        text->setText(std::string("Loading ")+frontFileName);

        osg::Geode* geode = new osg::Geode;
        geode->addDrawable(text);
        
        osg::PagedLOD* pagedlod = new osg::PagedLOD;
        pagedlod->setCenter(center);
        pagedlod->setRadius(1.6f);
        pagedlod->setNumChildrenThatCannotBeExpired(2);
        
        pagedlod->setRange(0,max_visible_distance,1e7);
        pagedlod->addChild(geode);
        
        pagedlod->setRange(1,cut_off_distance,max_visible_distance);
        pagedlod->setFileName(1,rw->insertReference(frontFileName,256,width,height,false));

        pagedlod->setRange(2,0.0f,cut_off_distance);
        pagedlod->setFileName(2,rw->insertReference(frontFileName,1024,width,height,false));

        front_page->addChild(pagedlod);
    }
     
     
    // set up back of page.
    osg::Group* back_page = new osg::Group;
    _switch->addChild(back_page);

    {

        osg::Geometry* geom = new osg::Geometry;
        geom->setStateSet(album->getBackgroundStateSet());

        osg::Vec3Array* coords = new osg::Vec3Array(4);
        (*coords)[0].set(width,0.0,height);
        (*coords)[1].set(width,0.0,0);
        (*coords)[2].set(0.0f,0.0,0);
        (*coords)[3].set(0.0f,0.0,height);
        geom->setVertexArray(coords);

        osg::Vec3Array* normals = new osg::Vec3Array(1);
        (*normals)[0].set(0.0f,1.0f,0.0f);
        geom->setNormalArray(normals);
        geom->setNormalBinding(osg::Geometry::BIND_OVERALL);

        osg::Vec2Array* tcoords = new osg::Vec2Array(4);
        (*tcoords)[0].set(1.0f,1.0f);
        (*tcoords)[1].set(1.0f,0.0f);
        (*tcoords)[2].set(0.0f,0.0f);
        (*tcoords)[3].set(0.0f,1.0f);
        geom->setTexCoordArray(0,tcoords);

        osg::Vec4Array* colours = new osg::Vec4Array(1);
        (*colours)[0].set(1.0f,1.0f,1.0,1.0f);
        geom->setColorArray(colours);
        geom->setColorBinding(osg::Geometry::BIND_OVERALL);

        geom->addPrimitiveSet(new osg::DrawArrays(osg::PrimitiveSet::QUADS,0,4));

        // set up the geode.
        osg::Geode* geode = new osg::Geode;
        geode->addDrawable(geom);
        
    
        back_page->addChild(geode);
    }

    if (!backFileName.empty())
    {
        float cut_off_distance = 8.0f;
        float max_visible_distance = 300.0f;
        
        osg::Vec3 center(width*0.5f,0.0f,height*0.5f);

        osgText::Text* text = new osgText::Text;
        text->setFont("fonts/arial.ttf");
        text->setPosition(center);
        text->setCharacterSize(height/20.0f);
        text->setAlignment(osgText::Text::CENTER_CENTER);
        text->setAxisAlignment(osgText::Text::REVERSED_XZ_PLANE);
        text->setColor(osg::Vec4(1.0f,1.0f,0.0f,1.0f));
        text->setText(std::string("Loading ")+backFileName);

        osg::Geode* geode = new osg::Geode;
        geode->addDrawable(text);
        
        osg::PagedLOD* pagedlod = new osg::PagedLOD;
        pagedlod->setCenter(center);
        pagedlod->setRadius(1.6f);
        pagedlod->setNumChildrenThatCannotBeExpired(2);
        
        pagedlod->setRange(0,max_visible_distance,1e7);
        pagedlod->addChild(geode);
        
        pagedlod->setRange(1,cut_off_distance,max_visible_distance);
        pagedlod->setFileName(1,rw->insertReference(backFileName,256,width,height,true));

        pagedlod->setRange(2,0.0f,cut_off_distance);
        pagedlod->setFileName(2,rw->insertReference(backFileName,1024,width,height,true));

        back_page->addChild(pagedlod);
    }

    addChild(_switch.get());
}

void Page::traverse(osg::NodeVisitor& nv)
{
    // if app traversal update the frame count.
    if (nv.getVisitorType()==osg::NodeVisitor::UPDATE_VISITOR)
    {
        const osg::FrameStamp* framestamp = nv.getFrameStamp();
        if (framestamp)
        {
            double t = framestamp->getReferenceTime();
            
            if (_rotation!=_targetRotation)
            {
                if (t>=_targetTime) _rotation = _targetRotation;
                else _rotation += (_targetRotation-_rotation)*(t-_lastTimeTraverse)/(_targetTime-_lastTimeTraverse);
                
                dirtyBound();
            }
            
            _lastTimeTraverse = t;

        }
    }
    Transform::traverse(nv);
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////

Album::Album(osg::ArgumentParser& arguments, float width, float height)
{


    typedef std::vector<std::string> FileList;
    FileList fileList;

    for(int pos=1;pos<arguments.argc();++pos)
    {
        if (arguments.isString(pos)) 
        {
            std::string filename(arguments[pos]);
            if (osgDB::getLowerCaseFileExtension(filename)=="album")
            {
                PhotoArchive* photoArchive = PhotoArchive::open(filename);
                if (photoArchive)
                {
                    g_ImageReaderWriter.get()->addPhotoArchive(photoArchive);
                    photoArchive->getImageFileNameList(fileList);
                }
                
            }
            else
            {
                fileList.push_back(arguments[pos]);
            }
        }
    }
    
    _radiusOfRings = 0.02;
    _startAngleOfPages = 0.0f;
    _deltaAngleBetweenPages = osg::PI/(float)fileList.size();
    
    _group = new osg::Group;
    _group->getOrCreateStateSet()->setAttributeAndModes(new osg::CullFace,osg::StateAttribute::ON);
    
    _backgroundStateSet = new osg::StateSet;
    _backgroundStateSet->setAttributeAndModes(new osg::PolygonOffset(1.0f,1.0f),osg::StateAttribute::ON);
    
    // load the images.
    unsigned int i;
    for(i=0;i<fileList.size();i+=2)
    {
        Page* page = i+1<fileList.size()?
                     Page::createPage(this,_pages.size(),fileList[i],fileList[i+1], width, height):
                     Page::createPage(this,_pages.size(),fileList[i],"", width, height);
        if (page)
        {
            _pages.push_back(page);
            _group->addChild(page);
        }
    }
    
    setVisibility();

}

osg::Matrix Album::getPageOffset(unsigned int pageNo) const
{
    float angleForPage = _startAngleOfPages+_deltaAngleBetweenPages*(float)pageNo;
    osg::Vec3 delta(_radiusOfRings*sinf(angleForPage),-_radiusOfRings*cosf(angleForPage),0.0f);
    return osg::Matrix::translate(delta);
}

bool Album::gotoPage(unsigned int pageNo, float timeToRotateBy)
{
    if (pageNo>=_pages.size()) return false;

    if (pageNo>_currentPageNo)
    {
        for(unsigned int i=_currentPageNo;i<pageNo;++i)
        {
            _pages[i]->rotateTo(osg::PI,timeToRotateBy);
        }
        _currentPageNo = pageNo;
        
        return true;
    }
    else if (pageNo<_currentPageNo)
    {
        for(unsigned int i=pageNo;i<_currentPageNo;++i)
        {
            _pages[i]->rotateTo(0,timeToRotateBy);
        }
        _currentPageNo = pageNo;
        
        return true;
    }
    
    return false;
}

void Album::setVisibility()
{
    for(unsigned int i=0;i<_pages.size();++i)
    {
        bool front_visible = _pages[i]->rotating() ||
                             (i>0?_pages[i-1]->rotating():false) ||
                             i==_currentPageNo ||
                             i==0;

        bool back_visible = _pages[i]->rotating() ||
                            ((i+1)<_pages.size()?_pages[i+1]->rotating():false) ||
                            i==_currentPageNo-1 ||
                            i==_pages.size()-1;
    
        _pages[i]->setPageVisible(front_visible,back_visible);
    }
    
}


////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////////


class SlideEventHandler : public osgGA::GUIEventHandler
{
public:

    SlideEventHandler();
    
    META_Object(osgStereImageApp,SlideEventHandler);

    void set(Album* album, float timePerSlide, bool autoSteppingActive);

    virtual void accept(osgGA::GUIEventHandlerVisitor& v) { v.visit(*this); }

    virtual bool handle(const osgGA::GUIEventAdapter& ea,osgGA::GUIActionAdapter&);
    
    virtual void getUsage(osg::ApplicationUsage& usage) const;

protected:

    ~SlideEventHandler() {}
    SlideEventHandler(const SlideEventHandler&,const osg::CopyOp&) {}

    osg::ref_ptr<Album>         _album;
    bool                        _firstTraversal;
    double                      _previousTime;
    double                      _timePerSlide;
    bool                        _autoSteppingActive;
};

SlideEventHandler::SlideEventHandler():
    _album(0),
    _firstTraversal(true),
    _previousTime(-1.0f),
    _timePerSlide(5.0),
    _autoSteppingActive(false)
{
}

void SlideEventHandler::set(Album* album, float timePerSlide, bool autoSteppingActive)
{
    _album = album;

    _timePerSlide = timePerSlide;
    _autoSteppingActive = autoSteppingActive;    
    
}

bool SlideEventHandler::handle(const osgGA::GUIEventAdapter& ea,osgGA::GUIActionAdapter&)
{
    switch(ea.getEventType())
    {
        case(osgGA::GUIEventAdapter::KEYDOWN):
        {
            if (ea.getKey()=='a')
            {
                _autoSteppingActive = !_autoSteppingActive;
                _previousTime = ea.getTime();
                return true;
            }
            else if (ea.getKey()=='n')
            {
                _album->nextPage(ea.getTime()+1.0f);
                return true;
            }
            else if (ea.getKey()=='p')
            {
                _album->previousPage(ea.getTime()+1.0f);
                return true;
            }
            return false;
        }
        case(osgGA::GUIEventAdapter::FRAME):
        {
            if (_autoSteppingActive)
            {
                if (_firstTraversal)
                {
                    _firstTraversal = false;
                    _previousTime = ea.getTime();
                }
                else if (ea.getTime()-_previousTime>_timePerSlide)
                {
                    _previousTime = ea.getTime();

                    _album->nextPage(ea.getTime()+1.0f);
                }
            }
            
            _album->setVisibility();

        }

        default:
            return false;
    }
}

void SlideEventHandler::getUsage(osg::ApplicationUsage& usage) const
{
    usage.addKeyboardMouseBinding("Space","Reset the image position to center");
    usage.addKeyboardMouseBinding("a","Toggle on/off the automatic advancement for image to image");
    usage.addKeyboardMouseBinding("n","Advance to next image");
    usage.addKeyboardMouseBinding("p","Move to previous image");
}

int main( int argc, char **argv )
{

    // use an ArgumentParser object to manage the program arguments.
    osg::ArgumentParser arguments(&argc,argv);
    
    // set up the usage document, in case we need to print out how to use this program.
    arguments.getApplicationUsage()->setDescription(arguments.getApplicationName()+" is the example which demonstrates use node masks to create stereo images.");
    arguments.getApplicationUsage()->setCommandLineUsage(arguments.getApplicationName()+" [options] image_file [image_file]");
    arguments.getApplicationUsage()->addCommandLineOption("-d <float>","Time delay in sceonds between the display of successive image pairs when in auto advance mode.");
    arguments.getApplicationUsage()->addCommandLineOption("-a","Enter auto advance of image pairs on start up.");
    arguments.getApplicationUsage()->addCommandLineOption("-h or --help","Display this information");
    arguments.getApplicationUsage()->addCommandLineOption("--create <filename>","Create an photo archive of specified files");
    

    // construct the viewer.
    osgProducer::Viewer viewer(arguments);

    // set up the value with sensible default event handlers.
    //viewer.setUpViewer(osgProducer::Viewer::ESCAPE_SETS_DONE);
    viewer.setUpViewer();

    // register the handler to add keyboard and mosue handling.
    SlideEventHandler* seh = new SlideEventHandler();
    viewer.getEventHandlerList().push_front(seh);


    // get details on keyboard and mouse bindings used by the viewer.
    viewer.getUsage(*arguments.getApplicationUsage());

    // read any time delay argument.
    float timeDelayBetweenSlides = 5.0f;
    while (arguments.read("-d",timeDelayBetweenSlides)) {}

    bool autoSteppingActive = false;
    while (arguments.read("-a")) autoSteppingActive = true;

    // if user request help write it out to cout.
    if (arguments.read("-h") || arguments.read("--help"))
    {
        arguments.getApplicationUsage()->write(std::cout);
        return 1;
    }

    std::string archiveName;
    while (arguments.read("--create",archiveName)) {}
    
    // any option left unread are converted into errors to write out later.
    arguments.reportRemainingOptionsAsUnrecognized();

    // report any errors if they have occured when parsing the program aguments.
    if (arguments.errors())
    {
        arguments.writeErrorMessages(std::cout);
        return 1;
    }
    
    if (arguments.argc()<=1)
    {
        arguments.getApplicationUsage()->write(std::cout,osg::ApplicationUsage::COMMAND_LINE_OPTION);
        return 1;
    }


    if (!archiveName.empty())
    {
        // archive name set to create
        PhotoArchive::FileNameList fileNameList;
        for(int i=1;i<arguments.argc();++i)
        {
            if (arguments.isString(i)) fileNameList.push_back(std::string(arguments[i]));
        }
        
        PhotoArchive::buildArchive(archiveName,fileNameList);
        
        return 0;
    }


    // now the windows have been realized we switch off the cursor to prevent it
    // distracting the people seeing the stereo images.
    float fovx = 1.25f;
    float fovy = 1.0f;
    for( unsigned int i = 0; i < viewer.getNumberOfCameras(); i++ )
    {
        Producer::Camera* cam = viewer.getCamera(i);
        //Producer::RenderSurface* rs = cam->getRenderSurface();
        //rs->useCursor(false);
        fovx = cam->getLensHorizontalFov();
        fovy = cam->getLensVerticalFov();
    }

    float radius = 1.0f;
    float width = 2*radius*tan(fovx*0.5f);
    float height = 2*radius*tan(fovy*0.5f);

    osg::ref_ptr<Album> album = new Album(arguments,width,height);

    // creat the scene from the file list.
    osg::ref_ptr<osg::Group> rootNode = album->getScene();
    
    if (!rootNode) return 0;


    //osgDB::writeNodeFile(*rootNode,"test.osg");

    // set the scene to render
    viewer.setSceneData(album->getScene());


    // set up the SlideEventHandler.
    seh->set(album.get(),timeDelayBetweenSlides,autoSteppingActive);
    

    // create the windows and run the threads.
    viewer.realize();
    
    osg::Matrix homePosition;
    homePosition.makeLookAt(osg::Vec3(0.0f,0.0f,0.0f),osg::Vec3(0.0f,1.0f,0.0f),osg::Vec3(0.0f,0.0f,1.0f));
        
    while( !viewer.done() )
    {
        // wait for all cull and draw threads to complete.
        viewer.sync();

        // update the scene by traversing it with the the update visitor which will
        // call all node update callbacks and animations.
        viewer.update();
         
        //viewer.setView(homePosition);

        // fire off the cull and draw traversals of the scene.
        viewer.frame();
        
    }
    
    // wait for all cull and draw threads to complete before exit.
    viewer.sync();
    
    return 0;
}

