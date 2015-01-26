/*
 * ui_processing.cc
 *
 *  Created on: Dec 17, 2011
 *      Author: mfleder
 */
#include <gdk/gdkkeysyms.h>
#include "UIProcessing.h"
#include "LinearAlgebra.h"
#include "SurrogateException.h"
#include "PclSurrogateUtils.h"
#include <lcmtypes/drc_lcmtypes.hpp>
#include <iostream>

#include <pcl/common/distances.h>
#include <pcl/common/centroid.h>
#include <pcl/common/transforms.h>

#include <maps/BotWrapper.hpp>
#include <affordance/AffordanceUpWrapper.h>
#include <drc_utils/Clock.hpp>
#include <drc_utils/PointerUtils.hpp>
#include <drc_utils/PointConvert.h>

#include <affordance/AffordanceUtils.hpp>

using namespace std;
using namespace pcl;
using namespace boost;
using namespace Eigen;
using namespace affordance;
using namespace drc::PointConvert;

namespace surrogate_gui
{

  // closes popup window
  static gboolean on_popup_close (GtkButton* button, GtkWidget* pWindow)
  {
    gtk_widget_destroy (pWindow);
    return TRUE;
  }

  int start_spy_counter=0;
  static void on_start_spy_clicked(GtkToggleToolButton *tb, void *user_data)
  {
    BotViewer *self = (BotViewer*) user_data;
    if (start_spy_counter >0){ // is there a better way than this counter?
      int i = ::system ("bot-spy &> /dev/null &");
    }
    start_spy_counter++;
  }

	//======================================
	//======================================
	//======================================
	//==============================constructor/destructor
  UIProcessing::UIProcessing(BotViewer *viewer, boost::shared_ptr<lcm::LCM> lcm, string kinect_channel) :
		_gui_state(SEGMENTING),
		_surrogate_renderer(viewer, BOT_GTK_PARAM_WIDGET(bot_gtk_param_widget_new())),
		_objTracker(),
		_mViewClient(new maps::ViewClient()),
    _updatingAffordanceSelectionMenu(false)
	{

	       //--setup view client
	       maps::BotWrapper::Ptr wrapper(new maps::BotWrapper(drc::PointerUtils::stdPtr(lcm)));
	       _mViewClient->setBotWrapper(wrapper);
	       _mViewClient->start();

               // set up affordance wrapper
               _affordance_wrapper.reset(new AffordanceUpWrapper(lcm));
               drc::Clock::instance()->setLcm(lcm);
               drc::Clock::instance()->setVerbose(false);
               
	  
		//===========================================
		//===========================================
		//=========menu setup
		BotGtkParamWidget *pw = _surrogate_renderer._pw;

		// 1. pull map
		bot_gtk_param_widget_add_buttons(pw, PARAM_NAME_PULL_MAP, NULL);

    // 2. Affordance selection
    bot_gtk_param_widget_add_enum(pw, PARAM_NAME_AFFORDANCE_SELECT, BOT_GTK_PARAM_WIDGET_MENU, 
                0,"None",0,NULL);
    // 3. geometric primitive
		bot_gtk_param_widget_add_enum(pw, PARAM_NAME_GEOMETRIC_PRIMITIVE, BOT_GTK_PARAM_WIDGET_MENU,
					      //CAR, // initial value
					      0, // initial value
					      "Cylinder", CYLINDER,
					      "Sphere", SPHERE,
                                              "3D Circle", CIRCLE_3D,
					      "Plane", PLANE,
					      "Car", CAR,
                "Steering Cylinder", STEERING_CYL,
                                              "Standpipe", STANDPIPE,
					      "Line", LINE,
					      "Torus", TORUS,
					      "Cube", CUBE,
					      NULL);

		// 4. selection mode
		bot_gtk_param_widget_add_enum(pw, PARAM_NAME_MOUSE_MODE, BOT_GTK_PARAM_WIDGET_MENU,
									  0, //initial value
									  "Camera Move", CAMERA_MOVE,
									  "Rectangle Select", RECTANGLE_SELECT,
									  NULL);
		//self->mouse_mode = CAMERA_MOVE;

		// 5. affordance publish button
		bot_gtk_param_widget_add_buttons(pw, PARAM_NAME_AFFORDANCE_PUB, NULL);

		// 6. affordance push button
		bot_gtk_param_widget_add_buttons(pw, PARAM_NAME_AFFORDANCE_PUSH, NULL);

    bot_gtk_param_widget_add_separator (pw, "");

		// DOF Controls
		Segmentation::FittingParams defaultFp; //default fitting params
		bot_gtk_param_widget_add_double(pw, PARAM_NAME_MIN_RADIUS, BOT_GTK_PARAM_WIDGET_SPINBOX, 
																		0.01, 10, 0.01, defaultFp.minRadius);
		bot_gtk_param_widget_add_double(pw, PARAM_NAME_MAX_RADIUS, BOT_GTK_PARAM_WIDGET_SPINBOX, 
																		0.1, 10, 0.1, defaultFp.maxRadius);		
		bot_gtk_param_widget_add_double(pw, PARAM_NAME_DISTANCE_THRESHOLD, BOT_GTK_PARAM_WIDGET_SPINBOX, 
																		0.01, 1, 0.01, defaultFp.distanceThreshold);

    // YPR settings
    /*
		bot_gtk_param_widget_add_double(pw, PARAM_NAME_YAW, BOT_GTK_PARAM_WIDGET_SPINBOX, 
																		-3.14, +3.14, 0.05, defaultFp.yaw);
		bot_gtk_param_widget_add_double(pw, PARAM_NAME_PITCH, BOT_GTK_PARAM_WIDGET_SPINBOX, 
																		-3.14, +3.14, 0.05, defaultFp.pitch);
		bot_gtk_param_widget_add_double(pw, PARAM_NAME_ROLL, BOT_GTK_PARAM_WIDGET_SPINBOX, 
																		-3.14, +3.14, 0.05, defaultFp.roll);
		bot_gtk_param_widget_add_double(pw, PARAM_NAME_MAX_ANGLE, BOT_GTK_PARAM_WIDGET_SPINBOX, 
																		0, 6.28, 0.05, defaultFp.maxAngle);
    */

		//pause
		//bot_gtk_param_widget_add_booleans(pw, BOT_GTK_PARAM_WIDGET_CHECKBOX, PARAM_NAME_CLOUD_PAUSE, 0, NULL);
		//self->paused = 0;

		// current object
		/*bot_gtk_param_widget_add_enum(pw, PARAM_NAME_CURR_OBJECT, BOT_GTK_PARAM_WIDGET_MENU,
									  0, //initial value
									  "None", NO_OBJECT,
									  NULL);
		//self->curr_object = NO_OBJECT;
    */

		// New object button
		//bot_gtk_param_widget_add_buttons(pw, PARAM_NAME_NEW_OBJECT, NULL);

		// Clear object button
		//bot_gtk_param_widget_add_buttons(pw, PARAM_NAME_CLEAR_OBJECT, NULL);

		// Tracking mode
		/*bot_gtk_param_widget_add_enum(pw, PARAM_NAME_TRACK_METHOD, BOT_GTK_PARAM_WIDGET_MENU,
									  0, //initial value
									  "ICP", ICP,
									  //"3D-only", TRACK_3D,
									  //"2D and 3D", TRACK_2D,
									  NULL);
		//self->track_mode = TRACK_3D;
    */

		//===live tracking button
		//bot_gtk_param_widget_add_buttons(pw, PARAM_NAME_TRACK_OBJECTS, NULL);

		//affordance save cloud button
		bot_gtk_param_widget_add_buttons(pw, PARAM_NAME_SAVE_CLOUD, NULL);

    bot_gtk_param_widget_add_booleans(pw, BOT_GTK_PARAM_WIDGET_CHECKBOX, PARAM_NAME_POINT_COLOR, 1, NULL);

		// master reset button
		//bot_gtk_param_widget_add_buttons(pw, PARAM_NAME_RESET, NULL);

		//Clear Warning Messages
		//bot_gtk_param_widget_add_buttons(pw, PARAM_NAME_CLEAR_WARNING_MSGS, NULL);

  // add custom TOP VIEW button
  GtkWidget *start_spy_button;
  start_spy_button = (GtkWidget *) gtk_tool_button_new_from_stock(GTK_STOCK_FIND);
  gtk_tool_button_set_label(GTK_TOOL_BUTTON(start_spy_button), "Bot Spy");
  gtk_tool_item_set_tooltip(GTK_TOOL_ITEM(start_spy_button), viewer->tips, "Launch Bot Spy", NULL);
  gtk_toolbar_insert(GTK_TOOLBAR(viewer->toolbar), GTK_TOOL_ITEM(start_spy_button), 4);
  gtk_widget_show(start_spy_button);
  g_signal_connect(G_OBJECT(start_spy_button), "clicked", G_CALLBACK(on_start_spy_clicked), viewer);
  on_start_spy_clicked(NULL, (void *) viewer);  

		//===========================================
		//===========================================
		//===========================================

		//other fields
		_lcmCpp = lcm;

                _lcmgl = bot_lcmgl_init(_lcmCpp->getUnderlyingLCM(), "fit point cloud");

		_button_states.shift_L_is_down = false;
		_button_states.shift_R_is_down = false;
		_button_states.ctrl_L_is_down = false;
		_button_states.ctrl_R_is_down = false;

		//=event handler
		_ehandler		 			= (BotEventHandler*) calloc(1, sizeof(BotEventHandler));
		_ehandler->name 			= strdup(std::string("S Mouse Handler").c_str());
		_ehandler->mouse_press 		= cb_mouse_press;
		_ehandler->mouse_release 	= cb_mouse_release;
		_ehandler->mouse_motion 	= cb_mouse_motion;
		_ehandler->enabled 			= 0;
		_ehandler->user 			= this;

		/*added by mfleder:
		_ehandler.key_press 	= cb_key_press;
		_ehandler.key_release 	= cb_key_release;*/
		bot_viewer_add_event_handler(viewer, _ehandler, 0); //moved from main, todo: mfleder: need to allocate ehandler?

		// create and register mode handler
		_mode_handler = (BotEventHandler*) calloc(1, sizeof(BotEventHandler));
		_mode_handler->name 			= strdup(std::string("Mode Control").c_str());
		_mode_handler->enabled 		= 1;
		_mode_handler->key_press 	= cb_key_press;
		_mode_handler->key_release 	= cb_key_release;
		_mode_handler->user 		= this;
		 bot_viewer_add_event_handler(viewer, _mode_handler, 1);


		//gui callbacks
		g_signal_connect (G_OBJECT (pw), "changed",
						  G_CALLBACK (cb_on_param_widget_changed_xyzrgb), this);

		//lcm callbacks
		//should conver to c++format
		ptools_pointcloud2_t_subscribe(lcm->getUnderlyingLCM(), 
					       kinect_channel.c_str(), 
					       cb_on_kinect_frame, this);

		on_param_widget_changed_xyzrgb(pw, PARAM_NAME_NEW_OBJECT, NULL); // create new object by default

    // subscribe to affordance plus collection to allow object updating
    lcm->subscribe("AFFORDANCE_PLUS_COLLECTION", &UIProcessing::affordanceMsgHandler, this);

	}

	UIProcessing::~UIProcessing()
	{
          bot_lcmgl_destroy(_lcmgl);
		free(_ehandler);
		free(_mode_handler);
	}
	//======================================
	//======================================
	//======================================
	//======================================
	//======================================mutators============
	//======================================
	//======================================
	//======================================
	//======================================

	//================segmentation
	/**Adds the given indices (into the sh->renderer->msg) to the current object
	 * @requires getCurrentObjectSelected != NULL*/
	void UIProcessing::addIndicesToCurrentObject(set<int> &selected_indices)
	{
		if (_gui_state != SEGMENTING || !_surrogate_renderer.isPaused())
			throw SurrogateException("addIndicesToCurrentObject: should be segmenting and paused");

		//get the current object
		ObjectPointsPtr currObj = getCurrentObjectSelected();
		if (currObj == ObjectPointsPtr())
		{
			_surrogate_renderer.setWarningText("No Object Selected for Storing Segmentation!!! Please hit 'New Object'");
			return;
		}

		// Insert the indices into the current view and intersection
		for(set<int>::iterator i = selected_indices.begin();
			i != selected_indices.end(); ++i)
		{
			currObj->indices->insert(*i);
		}

		return;
	}

	void UIProcessing::removeIndicesFromCurrentObject(set<int> &selected_indices)
	{
		if (_gui_state != SEGMENTING || !_surrogate_renderer.isPaused())
			throw SurrogateException("removeIndicesFromCurrentObject: should be segmenting and paused");

		//get the current object
		ObjectPointsPtr currObj = getCurrentObjectSelected();
		if (currObj == ObjectPointsPtr())
		{
			_surrogate_renderer.setWarningText("No Object Selected for Storing Segmentation!!! Please hit 'New Object'");
			return;
		}

		// Insert the indices into the current view and intersection
		for(set<int>::iterator i = selected_indices.begin();
			i != selected_indices.end(); ++i)
		{
			currObj->indices->erase(*i);
		}

		return;
	}

	/**@requires getCurrentObjectSelected() != null*/
	void UIProcessing::intersectViews(set<int> &selected_indices)
	{
		if (_gui_state != SEGMENTING || !_surrogate_renderer.isPaused())
			throw SurrogateException("intersect views: should be segmenting and paused");

		ObjectPointsPtr currObj = getCurrentObjectSelected();
		if (currObj == ObjectPointsPtr())
		{
			_surrogate_renderer.setWarningText("No Object Selected for Storing Segmentation!!! Please hit 'New Object'");
			return;
		}

		SetIntPtr intersect (new set<int>);
		std::set_intersection(
							currObj->indices->begin(),
							currObj->indices->end(),
							selected_indices.begin(),
							selected_indices.end(),
							std::inserter(*intersect, intersect->begin() ) );

		currObj->indices = intersect;
	}

	void UIProcessing::suggestSegments()
	{
		if (_gui_state != SEGMENTING || !_surrogate_renderer.isPaused())
			throw SurrogateException("should be segmenting and paused");

		//get the current object
		ObjectPointsPtr currObj = getCurrentObjectSelected();

		if (currObj == ObjectPointsPtr() || currObj->indices->size() == 0)
		{
			//warning should have been already printed if null
			//can't do anything if no indices
			return;
		}

		vector<PointIndices::Ptr> planes = Segmentation::segment(_surrogate_renderer._display_info.cloud,
									 currObj->indices);
		if (planes.size() == 0)
		{
			_surrogate_renderer.setHintText("");
			//_surrogate_renderer.setHintText("No planar components found");
			return;
		}

		currObj->auto_segment_indices.clear(); //get rid of existing plane segment suggestions
		for (uint u = 0; u < planes.size(); u++)
		{
			//todo: use shared ptrs
			PointIndices::Ptr nextPlane = planes[u];
			SetIntPtr nextPlaneSet (new set<int>);
			for (uint i = 0; i < nextPlane->indices.size(); i++)
			{
				nextPlaneSet->insert(nextPlane->indices[i]);
			}

			currObj->auto_segment_indices.push_back(nextPlaneSet);
		}

		_surrogate_renderer.setHintText(Color::to_string(currObj->auto_segment_indices.size())
										  + " segment suggestions");
	}

	//======================================mouse
	//======================================
	//======================================
	//======================================

	/**handle mouse button clicks*/
	int UIProcessing::mouse_press  (BotViewer *viewer, BotEventHandler *ehandler,
								   const double ray_start[3], const double ray_dir[3], const GdkEventButton *event)
	{
		//SegHandler *sh = (SegHandler*) ehandler->user;
		if (event->button == 1 && getMouseMode() == RECTANGLE_SELECT) //left button press and rectangle select?
		{
			Rectangle2D *rect = &(getDisplayInfo()->rectangle2D);
			rect->shouldDraw = 1;
		rect->x0 = event->x;
			rect->y0 = event->y;
			rect->x1 = event->x;
			rect->y1 = event->y;
		}

		return 1;
	}

	int UIProcessing::mouse_release (BotViewer *viewer, BotEventHandler *ehandler,
									 const double ray_start[3], const double ray_dir[3], const GdkEventButton *event)
	{
		if (event->button == 1 && getMouseMode() == RECTANGLE_SELECT) //left button press and rectangle select?
		{
			//--------get rectangle selection and update rectangle display
			Rectangle2D *rect = &(getDisplayInfo()->rectangle2D);
			rect->x1 = event->x;
			rect->y1 = event->y;
			rect->shouldDraw = 0;
			set<int> selected_indices = _surrogate_renderer.mouseRectTo3D(rect->x0, rect->y0, rect->x1, rect->y1);

			//cout << endl << "selected intersect indices size: " << selected_indices.size() << endl;

			//object selected for messing w/ indices?
			ObjectPointsPtr currObj = getCurrentObjectSelected();
			if ( currObj == ObjectPointsPtr())
			{
				_surrogate_renderer.setWarningText("No Object Selected for Storing Segmentation!!! Please hit 'New Object'");
				return 1;
			}

			//user is editing whatever's being displayed
			currObj->indices = currObj->getSegmentBeingDisplayed();
			currObj->clearAutoSegments(); //get rid of old auto-segments, since we're about to modify

			if (_button_states.shift_L_is_down || _button_states.shift_R_is_down)
				addIndicesToCurrentObject(selected_indices);
			else if (_button_states.ctrl_L_is_down || _button_states.ctrl_R_is_down)
				removeIndicesFromCurrentObject(selected_indices);
			else if (selected_indices.size() != 0)//intersect
				intersectViews(selected_indices);
			else if (selected_indices.size() == 0) //empty intersection, clear
			{
				currObj->indices->clear();
				_surrogate_renderer.getModelInfo()->circles.clear();
				_surrogate_renderer.getModelInfo()->lines.clear();
				_surrogate_renderer.setHintText("Selection Cleared");
				return 1;
			}

			//suggestSegments();
			currObj->setDisplayFullPointSet(); //show full point set initially
			bot_viewer_request_redraw(_surrogate_renderer._viewer);
		}

		//===============right button click?
		if (event->button == 3 && getMouseMode() == RECTANGLE_SELECT
			&& _gui_state == SEGMENTING)
		{
			if (stringsEqual(getCurrentObjectSelectedName(), "None") || getCurrentObjectSelected()->indices->size() == 0)
				return 1; //nothing segmented, nothing to do

			//---display a menu
			GtkWidget *menu 	= gtk_menu_new();
			GtkWidget *deleteSegment = gtk_menu_item_new_with_label("Delete Segment");

			g_signal_connect(deleteSegment, "activate",
							(GCallback) cb_right_click_popup_menu, this);
			gtk_menu_shell_append(GTK_MENU_SHELL(menu), deleteSegment);

			gtk_widget_show_all(menu);

			gtk_menu_popup(GTK_MENU(menu),
						   NULL, NULL, NULL, NULL,
						   event->button,
						   gdk_event_get_time((GdkEvent*) event));
		}

	  return 1;
	}

	int UIProcessing::mouse_motion(BotViewer *viewer, BotEventHandler *ehandler,
								  const double ray_start[3], const double ray_dir[3], const GdkEventMotion *event)
	{
		//SegHandler *sh = (SegHandler*) ehandler->user;
		Rectangle2D *rect = &(getDisplayInfo()->rectangle2D);
		if (rect->shouldDraw)
		{
			rect->x1 = event->x;
			rect->y1 = event->y;
			bot_viewer_request_redraw(_surrogate_renderer._viewer);
		}

	  return 1;
	}

	/**Handles right-click pop-up menu items*/
	void UIProcessing::right_click_popup(GtkWidget *menuItem)
	{
		//get which menu item was clicked
		string label(gtk_menu_item_get_label((GtkMenuItem*) menuItem));

		if (stringsEqual(label, "Delete Segment"))
		{
			//----defensive checks
			if (_gui_state != SEGMENTING)
				throw SurrogateException("Menu should not have been constructed outside of Segmenting state");
			if (stringsEqual(getCurrentObjectSelectedName(), "None"))
				throw SurrogateException("No object selected for deleting");

			ObjectPointsPtr currObj = getCurrentObjectSelected();
			if (currObj->indices->size() == 0)
				throw SurrogateException("shouldn't have displayed this menu option: delete segment. No indices");

			//----actually delete
			currObj->deleteCurrentSegment();

			//--------update user
			_surrogate_renderer.setHintText(Color::to_string(currObj->auto_segment_indices.size())
												  + " segment suggestions");
		}
	}

	//=================================================
	//=================================================
	//=================================================
	//=================================================
	//=================================================
	//=================================key presses
	/**Handles key presses*/
	int UIProcessing::key_press (BotViewer *viewer, BotEventHandler *ehandler, const GdkEventKey *event)
	{
		GraspInfo *gi = _surrogate_renderer.getGraspInfo();
		AdjustableVector *fvec = &(gi->forceVector);
		AdjustableVector *rvec = &(gi->rotationAxis);
		if (fvec->displaySelected() == rvec->displaySelected())
		{
			fvec->displaySelected(true);
			rvec->displaySelected(false);
		}
		AdjustableVector *vec = fvec->displaySelected()
							   ? fvec : rvec;
		switch (event->keyval)
		{
			case GDK_1:
				_surrogate_renderer.setCamera(CAMERA_FRONT);
				break;

			case GDK_2:
				_surrogate_renderer.setCamera(CAMERA_SIDE);
				break;

			case GDK_3:
				_surrogate_renderer.setCamera(CAMERA_TOP);
				break;

			case GDK_Shift_L:
				_button_states.shift_L_is_down = true;
				break;
			case GDK_Shift_R:
				_button_states.shift_R_is_down = true;
				break;

			case GDK_Control_L:
				_button_states.ctrl_L_is_down = true;
				break;
			case GDK_Control_R:
				_button_states.ctrl_R_is_down = true;
				break;
			case GDK_Tab:
			{
				if (_gui_state == TRACKING)
				{
					//flip each state
					//Top of function guarantees they are in different states
					fvec->displaySelected(!fvec->displaySelected());
					rvec->displaySelected(!rvec->displaySelected());
				}
				if (fvec->displaySelected())
					_surrogate_renderer.setWarningText("Force Vector Selected");
				else
					_surrogate_renderer.setWarningText("Rotation Axis Selected");

				break;
			}
				break;
			case GDK_a:
			{
				if (_gui_state != TRACKING)
					_surrogate_renderer.setWarningText("'a' unmapped key in segmenting state");
				else
					vec->_displayAxes = !vec->_displayAxes;
				break;
			}
			case GDK_g:
			{
				if (_gui_state != TRACKING)
					_surrogate_renderer.setWarningText("'g' key unmapped in segmenting state");
				else if (!gi->forceVector.updateOrientationCalled() ||
						!gi->forceVector.basisInitialized() ||
						!gi->rotationAxis.updateOrientationCalled() ||
						!gi->rotationAxis.basisInitialized())
					_surrogate_renderer.setWarningText("won't generate a trajectory until you adjust force/rotation vectors");
				else
				_surrogate_renderer.getGraspInfo()->displayTrajectory =
							!_surrogate_renderer.getGraspInfo()->displayTrajectory;
			}

			case GDK_s:
			{
				UserSegmentation *user_seg_info = &(getDisplayInfo()->user_seg_info);
				user_seg_info->color_segments = !(user_seg_info->color_segments);
				break;
			}
			case GDK_c:
				bot_gtk_param_widget_set_enum(_surrogate_renderer._pw, PARAM_NAME_MOUSE_MODE, CAMERA_MOVE);
				break;

			case GDK_r:
				bot_gtk_param_widget_set_enum(_surrogate_renderer._pw, PARAM_NAME_MOUSE_MODE, RECTANGLE_SELECT);
				break;

			case GDK_t:
				if (_gui_state != TRACKING)
					_surrogate_renderer.setWarningText("Can't turn on/off tracking cloud if not tracking");
				else
				{
					TrackingInfo *ti = &(getDisplayInfo()->tracking_info);
					ti->displayTrackingCloud = !(ti->displayTrackingCloud);
				}
				break;

			case GDK_u:
				_surrogate_renderer.getModelInfo()->displayOn =
						!_surrogate_renderer.getModelInfo()->displayOn;
				break;
			case GDK_Left:
			{
				if (_gui_state == TRACKING)
				{
					if (_button_states.shift_L_is_down || _button_states.shift_R_is_down)
						vec->translateAlongAxis(0, -vec->TRANSLATE_INC, 0);
					else
						vec->updateOrientation(vec->ANGLE_INC, 0);
				}
				else if (_gui_state == SEGMENTING)
					getCurrentObjectSelected()->setDisplayPreviousSubSegment();
				else
					_surrogate_renderer.setWarningText("left arrow not implemented in this mode");
				break;
			}
			case GDK_Right:
			{
				if (_gui_state == TRACKING)
				{
					if (_button_states.shift_L_is_down || _button_states.shift_R_is_down)
						vec->translateAlongAxis(0, vec->TRANSLATE_INC, 0);
					else
						vec->updateOrientation(-vec->ANGLE_INC, 0);
				}
				else if (_gui_state == SEGMENTING)
					getCurrentObjectSelected()->setDisplayNextSubSegment();
				else
					_surrogate_renderer.setWarningText("right arrow not implemented in this mode");
				break;
			}
			case GDK_Up:
			{
				if (_gui_state == TRACKING)
				{
					if (_button_states.shift_L_is_down && _button_states.shift_R_is_down)
						vec->translateAlongAxis(vec->TRANSLATE_INC,0,0); //both shifts are down
					else if (_button_states.shift_L_is_down || _button_states.shift_R_is_down)
						vec->translateAlongAxis(0, 0, vec->TRANSLATE_INC); //1 shift is down
					else
						vec->updateOrientation(0, vec->ANGLE_INC);
				}
				else
					_surrogate_renderer.setWarningText("up arrow not implemented in this mode");
				break;
			}
			case GDK_Down:
			{
				if (_gui_state == TRACKING)
				{
					if (_button_states.shift_L_is_down && _button_states.shift_R_is_down)
						vec->translateAlongAxis(-vec->TRANSLATE_INC,0,0); //both shifts are down
					else if (_button_states.shift_L_is_down || _button_states.shift_R_is_down)
						vec->translateAlongAxis(0, 0, -vec->TRANSLATE_INC);
					else
						vec->updateOrientation(0, -vec->ANGLE_INC);
				}
				else
					_surrogate_renderer.setWarningText("up arrow not implemented in this mode");
				break;
			}
			case GDK_plus:
			{
				vec->updateLength(vec->LENGTH_INC);
				break;
			}

			case GDK_minus:
			{
				vec->updateLength(-vec->LENGTH_INC);
				break;
			}

			default:
				_surrogate_renderer.setWarningText("Unsupported key stroke");
				break;
		}

		bot_viewer_request_redraw(_surrogate_renderer._viewer);

		return 0;
	}

	int UIProcessing::key_release (BotViewer *viewer, BotEventHandler *ehandler, const GdkEventKey  *event)
	{
		//SegHandler* sh = (SegHandler*) ehandler->user;
		if (event->keyval == GDK_Shift_L)
			_button_states.shift_L_is_down = false;
		else if (event->keyval == GDK_Shift_R)
			_button_states.shift_R_is_down = false;
		else if (event->keyval == GDK_Control_L)
			_button_states.ctrl_L_is_down = false;
		else if (event->keyval == GDK_Control_R)
			_button_states.ctrl_R_is_down = false;
		return 1;
	}


	//================================
	//================================
	//================================
	//================================
	//================================
	//================================
	//=================lcm
  void UIProcessing::unpack_pointcloud2(const ptools_pointcloud2_t *msg,
					pcl::PointCloud<pcl::PointXYZRGB>::Ptr &cloud)
  {
    // 1. Copy fields - this duplicates /pcl/ros/conversions.h for "fromROSmsg"
    cloud->width   = msg->width;
    cloud->height   = msg->height;
    uint32_t num_points = msg->width * msg->height;

    cloud->points.resize (num_points);
    cloud->is_dense = false;//msg->is_dense;
    uint8_t* cloud_data = reinterpret_cast<uint8_t*>(&cloud->points[0]);
    //not used? uint32_t cloud_row_step = static_cast<uint32_t> (sizeof (pcl::PointXYZRGB) * cloud->width);
    const uint8_t* msg_data = &msg->data[0];
    memcpy (cloud_data, msg_data, msg->data_nbytes );

    // 2. HACK/Workaround
    // for some reason in pcl1.5/6, this callback results
    // in RGB data whose offset is not correctly understood
    // Instead of an offset of 12bytes, its offset is set to be 16
    // this fix corrects for the issue:
    sensor_msgs::PointCloud2 msg_cld;
    pcl::toROSMsg(*cloud, msg_cld);
    msg_cld.fields[3].offset = 12;
    pcl::fromROSMsg (msg_cld, *cloud);

    // Transform cloud to that its in robotic frame:
    // Could also apply the cv->robotic transform directly
    // removed by mfallon sept 2012:
    /*
    double y_temp;
    for(uint j=0; j<cloud->points.size(); j++) 
      {
      // forward is fine - x=x
	y_temp = cloud->points[j].y;
	cloud->points[j].y =- cloud->points[j].z;
	cloud->points[j].z = y_temp;
	cloud->points[j].x = cloud->points[j].x;
      //cloud->points[j].z = x_temp;
      }*/
  }
  

	// On Kinect frame, copy data into msg
	void UIProcessing::on_kinect_frame (const lcm_recv_buf_t *rbuf, const char *channel,
					    const ptools_pointcloud2_t *msg, void *user_data )
	{
		if (_surrogate_renderer.isPaused())
			return; //discard any new messages, we're paused

		//lcm cloud
		cout << "\n\n about to try and unpacked\n\n" << endl;
		pcl::PointCloud<pcl::PointXYZRGB>::Ptr unpacked(new PointCloud<PointXYZRGB>);
		unpack_pointcloud2(msg, unpacked);

		cout << "\n\n unpacked cloud \n\n" << endl;

		getDisplayInfo()->cloud = unpacked;  //PclSurrogateUtils::toPcl(msg);
		getDisplayInfo()->lcmCloudFrameId = "header_not_set";//msg->header.frame_id;


		if (_gui_state == TRACKING && getTrackMode() == ICP)
		{
			_objTracker->findObjectInCloud(getDisplayInfo()->cloud);
			TrackingInfo *ti = &(getDisplayInfo()->tracking_info);

			ti->planeNormal 			= _objTracker->getPlaneNormal();
			ti->trackedObject 			= _objTracker->getTrackedObject()->makeShared();
			ti->trackedObjectTransform 	= _objTracker->getTrackedObjectTransform();
			ti->centroidTrace			= _objTracker->_centroidTrace;

			//-----update force vector start location
			GraspInfo *gi = _surrogate_renderer.getGraspInfo();
			ModelFit *model = _surrogate_renderer.getModelInfo();
			PointXYZ objCentroidLast = ti->centroidTrace.back();
			if (!gi->forceVector.updateOrientationCalled())
			{
				//determine intitial direction guess
				PointXYZ forceInitDir = model->haveModel
										? model->rotationAxisGuess.planeVec0
										: LinearAlgebra::mult(-1, LinearAlgebra::normalize(ti->planeNormal));

				PointXYZRGB minPt, maxPt; //points on the maximum diameter of the segment guess indices
				pcl::getMaxSegment (*_objTracker->getTrackedObject(),
				         		    minPt, maxPt);
				PointXYZ fOffset = LinearAlgebra::sub(objCentroidLast,
													  PointXYZ(minPt.x, minPt.y, minPt.z));

				gi->forceVector.set(forceInitDir,
						            objCentroidLast,
						            0.15,
						            fOffset);
			}
			else
				gi->forceVector.updateOrigin(objCentroidLast);

			//-----initialize rotation axis
			if (!gi->rotationAxis.updateOrientationCalled() && !gi->rotationAxis.translateCalled())
			{
				if (model->haveModel)
				{
					Circle3D *rCircle = &model->rotationAxisGuess;
					PointXYZ rotDir = LinearAlgebra::crossProduct(rCircle->planeVec0, rCircle->planeVec1);
					gi->rotationAxis.set(rotDir, rCircle->center, 0.15, PointXYZ(0,0,0));
				}
				else
					gi->rotationAxis.set(gi->forceVector.getVectorUnitDir(),
										 objCentroidLast,
										 0.18,
										 PointXYZ(0, 0, 0));
			}

			//---generate trajectory if everything has been updated
		}

		if (_gui_state == TRACKING)
			_surrogate_renderer.setModeText("Tracking");
		else if (_gui_state == SEGMENTING)
			_surrogate_renderer.setModeText("Segmenting");
		else
			_surrogate_renderer.setWarningText("Unknown state");

		bot_viewer_request_redraw(_surrogate_renderer._viewer);
	}


	//========================user menu/button interactions
	//===================
	//===================
	//===================
	//===================
	//===================
	void UIProcessing::handleMouseModeAdjust(BotEventHandler *default_handler, BotGtkParamWidget *pw)
	{
		//-------------are we tracking and the user tries to rectangle select?
		if (getMouseMode() == RECTANGLE_SELECT && _gui_state != SEGMENTING)
		{
			_surrogate_renderer.setWarningText("Can't Segment While Tracking!");

			// reverse the effect
			//_gui_state = SEGMENTING;
			bot_gtk_param_widget_set_enum(pw, PARAM_NAME_MOUSE_MODE, CAMERA_MOVE);
			//_gui_state = TRACKING;
			return;
		}


		if (getMouseMode() == CAMERA_MOVE)
		{
			default_handler->enabled = 1;
			_ehandler->enabled = 0;
			_surrogate_renderer._display_info.rectangle2D.shouldDraw = 0;
			printf("DEFAULT CAMERA MODE\n");
		}
		else if (getMouseMode () == RECTANGLE_SELECT && _gui_state == SEGMENTING)
		{
			default_handler->enabled = 0;
			_ehandler->enabled = 1;
			//pause the display
			bot_gtk_param_widget_set_bool(_surrogate_renderer._pw, PARAM_NAME_CLOUD_PAUSE, 1);
			printf("SEG MODE: RECTANGLE\n");
		}
		else
			throw SurrogateException("UNKNOWN MOUSE MODE!!!");
	}

	void UIProcessing::handleTrackLiveButton(BotGtkParamWidget *pw)
	{
		UserSegmentation *segInfo = _surrogate_renderer.getSegInfo();

		if (_gui_state == TRACKING)
		{
			_surrogate_renderer.setWarningText("already tracking!");
			return;
		}

		// see if any object have been segmented?
		if (segInfo->objectPtsMap->size() == 0)
		{
			_surrogate_renderer.setWarningText("no segmented objects have been created!");
			return;
		}

		if (segInfo->objectPtsMap->size() >= 2)
		{
			_surrogate_renderer.setWarningText("Multiple Object Tracking Not Currently Supported.  Please Reset.");
			return;
		}

		// make sure the first object has some points
		SetIntPtr objIndices   = getCurrentObjectSelected()->getSegmentBeingDisplayed();
		if (objIndices->size() == 0)
		{
			_surrogate_renderer.setWarningText("no object points to track: current object is empty");
			return;
		}

		//clear model fitting
		_surrogate_renderer.getModelInfo()->displayOn = true;
		_surrogate_renderer.getModelInfo()->drawCircles = false;
		_surrogate_renderer.getModelInfo()->drawLines = false;
		_surrogate_renderer.getModelInfo()->circles.clear();
		_surrogate_renderer.getModelInfo()->lines.clear();

		// switch the gui state!
		_surrogate_renderer.setModeText("TRACKING");
		_surrogate_renderer.setHintText("");
		_gui_state = TRACKING;

		//ok, let's initialize the tracker
		ObjectTracker *objTrkr = new ObjectTracker(getDisplayInfo()->cloud, objIndices);
		_objTracker = ObjectTracker::Ptr(objTrkr);

		// unpause the feed
		bot_gtk_param_widget_set_bool(pw, PARAM_NAME_CLOUD_PAUSE, 0);

		// go into camera mode
		bot_gtk_param_widget_set_enum(pw, PARAM_NAME_MOUSE_MODE, CAMERA_MOVE);

		//clear the segmented objects: indices are no longer meaningful w/ different clouds
		segInfo->objectPtsMap->clear();

		_surrogate_renderer.getTrackInfo()->displayTrackingCloud 	= true;
	}

  void UIProcessing::handleSaveCloudButton(BotGtkParamWidget *pw)
  {
    // generate filename based on time
    char filename[100];
    time_t ttime = time(NULL);
    tm* tmtime = localtime(&ttime);
    strftime(filename, 100, "cloud_%y%m%d-%H%M%S.pcd", tmtime);

    // extract subcloud
    pcl::PointCloud<pcl::PointXYZRGB>::ConstPtr cloud = _surrogate_renderer._display_info.cloud;
    ObjectPointsPtr currObj = getCurrentObjectSelected();

    PointCloud<PointXYZRGB>::ConstPtr subcloud;
    if(currObj->indices->size()==0) subcloud = cloud;  // if nothing selected, output everything
    else subcloud = PclSurrogateUtils::extractIndexedPoints(currObj->indices, cloud);

    // subtract off centroid x,y
    /*Vector4f centroid;
    compute3DCentroid(*subcloud, centroid);
    Matrix4f transformation = Matrix4f::Identity();
    transformation(0,3)=-centroid[0];
    transformation(1,3)=-centroid[1];
    //transformation(2,3)=-centroid[2];
    PointCloud<PointXYZRGB>::Ptr subcloud2(new PointCloud<PointXYZRGB>());
    transformPointCloud(*subcloud, *subcloud2, transformation);
    subcloud = subcloud2;*/

    /*PointCloud<PointXYZRGB>::Ptr subcloud2(new PointCloud<PointXYZRGB>(*subcloud));
    for(int i=0;i<subcloud->size();i++){
      subcloud2->at(i).x += 0.8;
      subcloud2->at(i).y += 2.57;
    }
    subcloud = subcloud2;*/

    // write subcloud
    cout << "Write cloud: " << filename << endl;
    pcl::PCDWriter writer;
    writer.write (filename, *subcloud, false);

  }


  void UIProcessing::handleAffordancePubButton(BotGtkParamWidget *pw)
  {
    // clear warning text
    _surrogate_renderer.setWarningText("");

		Segmentation::FittingParams fp;
		fp.minRadius = bot_gtk_param_widget_get_double(pw, PARAM_NAME_MIN_RADIUS);
		fp.maxRadius = bot_gtk_param_widget_get_double(pw, PARAM_NAME_MAX_RADIUS);
		fp.distanceThreshold = bot_gtk_param_widget_get_double(pw, PARAM_NAME_DISTANCE_THRESHOLD);
    /* TODO: put back or remove altogether
		fp.yaw = bot_gtk_param_widget_get_double(pw, PARAM_NAME_YAW);
		fp.pitch = bot_gtk_param_widget_get_double(pw, PARAM_NAME_PITCH);
		fp.roll = bot_gtk_param_widget_get_double(pw, PARAM_NAME_ROLL);
		fp.maxAngle = bot_gtk_param_widget_get_double(pw, PARAM_NAME_MAX_ANGLE);
    */

    AffPlusPtr selectedAff = getSelectedAffordance();
    if(selectedAff){
      GeometricPrimitive prim = getGeometricPrimitive();
      string otdf = selectedAff->aff.otdf_type;

      // check for match
      bool match=false;
      if(prim==CYLINDER  && otdf=="cylinder") match=true;
      if(prim==SPHERE    && otdf=="sphere")   match=true;
      if(prim==CIRCLE_3D && otdf=="circle3d") match=true;
      if(prim==PLANE     && otdf=="plane")    match=true;
      if(prim==CAR       && otdf=="car")      match=true;
      if(prim==STEERING_CYL && otdf=="steering_cyl") match=true;
      if(prim==STANDPIPE && otdf=="standpipe") match=true;
      if(prim==LINE      && otdf=="TODO")     match=true;
      if(prim==TORUS     && otdf=="TODO")     match=true;
      if(prim==CUBE      && otdf=="TODO")     match=true;
      if(!match){
        _surrogate_renderer.setWarningText("Selected affordance does not match geometric primitive.");
        return;
      }

      // TODO: support selected afforance for other objects
      if(prim!=CAR || otdf!="car"){
        _surrogate_renderer.setWarningText("Warning: initial posistion is currrently ignored for this shape.");
      }
    }

    // TODO improve car support without seeding
    if(!selectedAff && getGeometricPrimitive()==CAR){
        _surrogate_renderer.setWarningText("Car fitting requires selected affordance.");
        return;
    }

	  switch(getGeometricPrimitive()){
	  case CYLINDER: handleAffordancePubButtonCylinder(fp); break;
	  case SPHERE:   handleAffordancePubButtonSphere(fp); break;
	  case CIRCLE_3D:handleAffordancePubButtonCircle3d(fp,CIRCLE_3D); break;
	  case PLANE:    handleAffordancePubButtonPlane(fp); break;
	  case CAR:      handleAffordancePubButtonPointCloud(fp,"car"); break;
	  case STEERING_CYL: handleAffordancePubButtonCircle3d(fp,STEERING_CYL); break;
	  case STANDPIPE: handleAffordancePubButtonPointCloud(fp,"standpipe"); break;
	  case LINE:     handleAffordancePubButtonLine(fp); break;
	  case TORUS:    handleAffordancePubButtonTorus(fp); break;
	  case CUBE:     handleAffordancePubButtonCube(fp); break;
	  default: 
	    _surrogate_renderer.setWarningText("Unexpected geometric primitive selected.");
	    break;
	  };
	  	  

  }
  
	void UIProcessing::handleAffordancePushButton(BotGtkParamWidget *pw)
	{
          AffPlusPtr selectedAff = getSelectedAffordance();
          if (selectedAff == NULL) {
            std::cout << "No affordance selected - not pushing anything" << std::endl;
            return;
          }
          std::vector<affordance::AffPlusPtr> affordances;
          _affordance_wrapper->getAllAffordancesPlus(affordances);
          drc::affordance_plus_collection_t msg;
          msg.name = "updateFromSegmentationViewer";
          msg.utime = drc::Clock::instance()->getCurrentTime();
          msg.map_id = -1;
          msg.naffs = affordances.size();
          for (int i = 0; i < msg.naffs; ++i) {
            drc::affordance_plus_t aff;
            affordances[i]->toMsg(&aff);
            if (affordances[i]->aff->_uid == selectedAff->aff.uid) {
              aff.aff.aff_store_control = drc::affordance_t::NEW;
            }
            else {
              aff.aff.aff_store_control = drc::affordance_t::UPDATE;
            }
            msg.affs_plus.push_back(aff);
          }
          _lcmCpp->publish(AffordanceServer::
                           AFFORDANCE_PLUS_BOT_OVERWRITE_CHANNEL, &msg);
          cout << "Sent affordance list" << endl;
	}


	void UIProcessing::handleAffordancePubButtonCylinder(const Segmentation::FittingParams& fp)
	{
    // retrieve initial pos from selected object if available
    Vector3f initialXYZ, initialYPR;
    bool isInitialSet = false;
    AffPlusPtr selectedAff = getSelectedAffordance();
    if(selectedAff){
      isInitialSet = true;
      convert3(selectedAff->aff.origin_xyz, initialXYZ);
      convert3(selectedAff->aff.origin_rpy, initialYPR);
      initialYPR.reverseInPlace();  //RPY to YPR
    }

	  //todo: map_utime, map_id, object_id
	  drc::affordance_plus_t affordanceMsg;
	  	  
	  affordanceMsg.aff.map_id = 0; 	  
	  affordanceMsg.aff.otdf_type = "cylinder";

          //geometrical properties
	  ObjectPointsPtr currObj = getCurrentObjectSelected();
	  double x,y,z,roll,pitch=0,yaw=0,radius,length=0.5;
	  std::vector<double> inliers_distances;
    std::vector< Vector3f > points;
    std::vector< Vector3i > triangles;
	  PointIndices::Ptr cylinderIndices 
	    = Segmentation::fitCylinder(_surrogate_renderer._display_info.cloud,
                                        currObj->indices, fp,
					x,y,z,
					roll,pitch,yaw,
					radius,
					length, 
          points, triangles, 
					inliers_distances);

	  affordanceMsg.aff.params.push_back(radius);
	  affordanceMsg.aff.param_names.push_back("radius");

	  affordanceMsg.aff.params.push_back(length);
	  affordanceMsg.aff.param_names.push_back("length");

	  affordanceMsg.aff.nparams = affordanceMsg.aff.params.size();


    // set origin and bounding
    convert3(Vector3f(x,y,z), affordanceMsg.aff.origin_xyz);
    convert3(Vector3f(roll,pitch,yaw), affordanceMsg.aff.origin_rpy);
    convert3(Vector3f(0,0,0), affordanceMsg.aff.bounding_xyz); 
    convert3(Vector3f(0,0,0), affordanceMsg.aff.bounding_rpy);
    convert3(Vector3f(radius*2,radius*2,length), affordanceMsg.aff.bounding_lwh); 

    // populate points and triangles
    affordanceMsg.npoints = 0;
    affordanceMsg.ntriangles = 0;
    //affordanceMsg.npoints = points.size();
    //affordanceMsg.ntriangles = triangles.size();
    //convertVec3(points, affordanceMsg.points);
    //convertVec3(triangles, affordanceMsg.triangles);

	  cout << "\n npoints = " << affordanceMsg.npoints << endl;

	  cout << "states.size() = " << affordanceMsg.aff.states.size() <<  " | state_names.size() = "
	       << affordanceMsg.aff.param_names.size() << endl;

	  //todo : Set these
	  //states: todo? is this used? states/state_names
	  affordanceMsg.aff.nstates = 0;
          affordanceMsg.aff.aff_store_control =drc::affordance_t::NEW; // added by mfallon march 2012
	  
    cout << "\n about to publish" << endl;
    if(selectedAff){
      affordanceMsg.aff.aff_store_control = drc::affordance_t::UPDATE;
      affordanceMsg.aff.uid = selectedAff->aff.uid;
      _lcmCpp->publish("AFFORDANCE_PLUS_TRACK", &affordanceMsg);
    }else{
      affordanceMsg.aff.aff_store_control =drc::affordance_t::NEW; 
	    _lcmCpp->publish("AFFORDANCE_FIT", &affordanceMsg);
    }
    cout << "\n ***published \n" << endl;
	  
	  return;
	}

	void UIProcessing::handleAffordancePubButtonSphere(const Segmentation::FittingParams& fp)
	{

    // retrieve initial pos from selected object if available
    Vector3f initialXYZ, initialYPR;
    bool isInitialSet = false;
    AffPlusPtr selectedAff = getSelectedAffordance();
    if(selectedAff){
      isInitialSet = true;
      convert3(selectedAff->aff.origin_xyz, initialXYZ);
      convert3(selectedAff->aff.origin_rpy, initialYPR);
      initialYPR.reverseInPlace();  //RPY to YPR
    }

	  //todo: map_utime, map_id, object_id
	  drc::affordance_plus_t affordanceMsg;
	  
	  affordanceMsg.aff.map_id = 0; 	  
	  affordanceMsg.aff.otdf_type = "sphere";

          //geometrical properties
	  ObjectPointsPtr currObj = getCurrentObjectSelected();
	  double x,y,z,radius;
          std::vector< vector<float> > inliers;
	  PointIndices::Ptr sphereIndices 
	    = Segmentation::fitSphere(_surrogate_renderer._display_info.cloud,
                                      currObj->indices, fp,
                                      x,y,z,
                                      radius,
                                      inliers);
	      
	  affordanceMsg.aff.params.push_back(radius);
	  affordanceMsg.aff.param_names.push_back("radius");
	  affordanceMsg.aff.nparams = affordanceMsg.aff.params.size();

    // set origin and bounding
    convert3(Vector3f(x,y,z), affordanceMsg.aff.origin_xyz);
    convert3(Vector3f(0,0,0), affordanceMsg.aff.origin_rpy);
    convert3(Vector3f(0,0,0), affordanceMsg.aff.bounding_xyz); 
    convert3(Vector3f(0,0,0), affordanceMsg.aff.bounding_rpy);
    convert3(Vector3f(radius*2,radius*2,radius*2), affordanceMsg.aff.bounding_lwh); 

	  //inliers
          affordanceMsg.npoints = 0;
          //affordanceMsg.npoints = inliers.size();
          //affordanceMsg.points = inliers;
          affordanceMsg.ntriangles = 0;
	  cout << "\n npoints = " << affordanceMsg.npoints << endl;

	  cout << "states.size() = " << affordanceMsg.aff.states.size() <<  " | state_names.size() = "
	       << affordanceMsg.aff.param_names.size() << endl;

	  //todo : Set these
	  //states: todo? is this used? states/state_names
	  affordanceMsg.aff.nstates = 0;
          affordanceMsg.aff.aff_store_control =drc::affordance_t::NEW; // added by mfallon march 2012
	  
    cout << "\n about to publish" << endl;
    if(selectedAff){
      affordanceMsg.aff.aff_store_control = drc::affordance_t::UPDATE;
      affordanceMsg.aff.uid = selectedAff->aff.uid;
      _lcmCpp->publish("AFFORDANCE_PLUS_TRACK", &affordanceMsg);
    }else{
      affordanceMsg.aff.aff_store_control =drc::affordance_t::NEW; 
	    _lcmCpp->publish("AFFORDANCE_FIT", &affordanceMsg);
    }
    cout << "\n ***published \n" << endl;
	  
	  return;
	}

	void UIProcessing::handleAffordancePubButtonCircle3d(const Segmentation::FittingParams& fp, GeometricPrimitive primitive)
	{

    // retrieve initial pos from selected object if available
    Vector3f initialXYZ, initialYPR;
    bool isInitialSet = false;
    AffPlusPtr selectedAff = getSelectedAffordance();
    if(selectedAff){
      isInitialSet = true;
      convert3(selectedAff->aff.origin_xyz, initialXYZ);
      convert3(selectedAff->aff.origin_rpy, initialYPR);
      initialYPR.reverseInPlace();  //RPY to YPR
    }

	  //todo: map_utime, map_id, object_id
	  drc::affordance_plus_t affordanceMsg;
	  	  
	  affordanceMsg.aff.map_id = 0;
    if(primitive == CIRCLE_3D) affordanceMsg.aff.otdf_type = "circle3d";
    else if(primitive == STEERING_CYL) affordanceMsg.aff.otdf_type = "steering_cyl";
    else {
      cout << "primitive not supported by handleAffordancePubButtonCircle3d: " << primitive << endl;
      return;
    }

          //geometrical properties
	  ObjectPointsPtr currObj = getCurrentObjectSelected();
	  double x,y,z,roll,pitch=0,yaw=0,radius,length=0.5;
          std::vector< vector<float> > inliers;
	  std::vector<double> inliers_distances; 
	  PointIndices::Ptr cylinderIndices 
	    = Segmentation::fitCircle3d(_surrogate_renderer._display_info.cloud,
																	currObj->indices, fp,
					x,y,z,
					roll,pitch,yaw,
					radius,
                                        inliers,
					inliers_distances);

		length = 0.01; // a 3d circle is a cylinder with 1 cm length
	      
	  affordanceMsg.aff.params.push_back(radius);
	  affordanceMsg.aff.param_names.push_back("radius");

	  affordanceMsg.aff.params.push_back(length);
	  affordanceMsg.aff.param_names.push_back("length");

	  affordanceMsg.aff.nparams = affordanceMsg.aff.params.size();

    // set origin and bounding
    convert3(Vector3f(x,y,z), affordanceMsg.aff.origin_xyz);
    convert3(Vector3f(roll,pitch,yaw), affordanceMsg.aff.origin_rpy);
    convert3(Vector3f(0,0,0), affordanceMsg.aff.bounding_xyz); 
    convert3(Vector3f(0,0,0), affordanceMsg.aff.bounding_rpy);
    convert3(Vector3f(radius*2,radius*2,0.01), affordanceMsg.aff.bounding_lwh); 

	  //inliers
          affordanceMsg.npoints = 0;
          //affordanceMsg.npoints = inliers.size();
          //affordanceMsg.points = inliers;
          affordanceMsg.ntriangles = 0;
	  cout << "\n npoints = " << affordanceMsg.npoints << endl;

	  cout << "states.size() = " << affordanceMsg.aff.states.size() <<  " | state_names.size() = "
	       << affordanceMsg.aff.param_names.size() << endl;

	  //todo : Set these
	  //states: todo? is this used? states/state_names
	  affordanceMsg.aff.nstates = 0;
          affordanceMsg.aff.aff_store_control =drc::affordance_t::NEW; // added by mfallon march 2012
	  
    cout << "\n about to publish" << endl;
    if(selectedAff){
      affordanceMsg.aff.aff_store_control = drc::affordance_t::UPDATE;
      affordanceMsg.aff.uid = selectedAff->aff.uid;
      _lcmCpp->publish("AFFORDANCE_PLUS_TRACK", &affordanceMsg);
    }else{
      affordanceMsg.aff.aff_store_control =drc::affordance_t::NEW; 
	    _lcmCpp->publish("AFFORDANCE_FIT", &affordanceMsg);
    }
    cout << "\n ***published \n" << endl;
	  
	  return;
	}

	void UIProcessing::handleAffordancePubButtonPlane(const Segmentation::FittingParams& fp)
	{
	  //todo: map_utime, map_id, object_id

    // retrieve initial pos from selected object if available
    Vector3f initialXYZ, initialYPR;
    bool isInitialSet = false;
    AffPlusPtr selectedAff = getSelectedAffordance();
    if(selectedAff){
      isInitialSet = true;
      convert3(selectedAff->aff.origin_xyz, initialXYZ);
      convert3(selectedAff->aff.origin_rpy, initialYPR);
      initialYPR.reverseInPlace();  //RPY to YPR
    }

          //geometrical properties
	  ObjectPointsPtr currObj = getCurrentObjectSelected();
	  //double x,y,z,roll,pitch=0,yaw=0,width=0.5,length=0.5;
          //vector< vector<float> > inliers;
	  //vector<double> inliers_distances; 
          //vector<Vector3f> convex_hull;
          vector<Segmentation::Plane> planeList;
	  //PointIndices::Ptr planeIndices = 
          Segmentation::fitPlanes(_surrogate_renderer._display_info.cloud,
                                 currObj->indices, fp,
                                 planeList);
          
          for(int i=0;i<planeList.size();i++){
            Segmentation::Plane& p = planeList[i];

            drc::affordance_plus_t affordanceMsg;
            
            affordanceMsg.aff.map_id = 0; 	  
            affordanceMsg.aff.otdf_type = "plane";
            //affordanceMsg.aff.otdf_type = "dynamic_mesh";
            
            affordanceMsg.aff.nparams = affordanceMsg.aff.params.size();
            
            /*
              int nPoints = 4;
              vector<Vector3f> points(nPoints);
              points[0] = Vector3f(-width/2, -length/2, 0);
              points[1] = Vector3f(-width/2,  length/2, 0);
              points[2] = Vector3f( width/2,  length/2, 0);
              points[3] = Vector3f( width/2, -length/2, 0);
            */
            
            
            // add points to message
            int nPoints = p.convexHull.size();
            affordanceMsg.npoints = nPoints;
            convertVec3(p.convexHull, affordanceMsg.points);
            
            // add triangles ot message
            affordanceMsg.ntriangles = nPoints-2;
            affordanceMsg.triangles.resize(affordanceMsg.ntriangles);
            for(int i=0; i<affordanceMsg.ntriangles; i++){
              Vector3i triangle(0,i+1,i+2);
              affordanceMsg.triangles[i].resize(3);
              convert3(triangle, affordanceMsg.triangles[i]);
            }

            // set origin and bounding
            convert3(p.xyz, affordanceMsg.aff.origin_xyz);
            convert3(p.ypr.reverse(), affordanceMsg.aff.origin_rpy);
            convert3(Vector3f(0,0,0), affordanceMsg.aff.bounding_xyz); 
            convert3(Vector3f(0,0,0), affordanceMsg.aff.bounding_rpy);
            convert3(Vector3f(p.width,p.length,0), affordanceMsg.aff.bounding_lwh); 

            cout << "\n npoints = " << affordanceMsg.npoints << endl;
            

            cout << "states.size() = " << affordanceMsg.aff.states.size() <<  " | state_names.size() = "
                 << affordanceMsg.aff.param_names.size() << endl;
            
            //todo : Set these
            //states: todo? is this used? states/state_names
            affordanceMsg.aff.nstates = 0;
            affordanceMsg.aff.aff_store_control =drc::affordance_t::NEW; // added by mfallon march 2012
            
            cout << "\n about to publish" << endl;
            if(selectedAff){
              affordanceMsg.aff.aff_store_control = drc::affordance_t::UPDATE;
              affordanceMsg.aff.uid = selectedAff->aff.uid;
              _lcmCpp->publish("AFFORDANCE_PLUS_TRACK", &affordanceMsg);
            }else{
              affordanceMsg.aff.aff_store_control =drc::affordance_t::NEW; 
	            _lcmCpp->publish("AFFORDANCE_FIT", &affordanceMsg);
            }
            cout << "\n ***published \n" << endl;
	  }

	  return;
	}

	void UIProcessing::handleAffordancePubButtonLine(const Segmentation::FittingParams& fp)
	{
	  handleAffordancePubButtonCylinder(fp); //placeholder TODO
	}

	void UIProcessing::handleAffordancePubButtonTorus(const Segmentation::FittingParams& fp)
	{
	  handleAffordancePubButtonCylinder(fp); //placeholder TODO
	}

	void UIProcessing::handleAffordancePubButtonCube(const Segmentation::FittingParams& fp)
	{
	  handleAffordancePubButtonCylinder(fp); //placeholder TODO
	}
    
  void UIProcessing::handleAffordancePubButtonPointCloud(const Segmentation::FittingParams& fp, const std::string& type)
	{

    // retrieve initial pos from selected object if available
    Vector3f initialXYZ, initialYPR;
    bool isInitialSet = false;
    AffPlusPtr selectedAff = getSelectedAffordance();
    if(selectedAff){
      isInitialSet = true;
      convert3(selectedAff->aff.origin_xyz, initialXYZ);
      convert3(selectedAff->aff.origin_rpy, initialYPR);
      initialYPR.reverseInPlace();  //RPY to YPR
    }

	  //todo: map_utime, map_id, object_id
	  drc::affordance_plus_t affordanceMsg;
	  affordanceMsg.aff.map_id = 0; 	  

          string modelfile;
          bool usefitPointCloudFPC;
          if(type == "car"){
            affordanceMsg.aff.otdf_type = "car";
            modelfile = "car.pcd";
            usefitPointCloudFPC = false;
            convert3(Vector3f(0,0,1),       affordanceMsg.aff.bounding_xyz); // center of car above ground 
            convert3(Vector3f(0,0,0),       affordanceMsg.aff.bounding_rpy);
            convert3(Vector3f(3.0,1.7,2.2), affordanceMsg.aff.bounding_lwh); // TODO make more general, not just car
          }else if(type == "standpipe"){
            affordanceMsg.aff.otdf_type = "standpipe::mate::firehose";
            modelfile = "standpipe.pcd";
            usefitPointCloudFPC = true;
            convert3(Vector3f(0,0,0),       affordanceMsg.aff.bounding_xyz); 
            convert3(Vector3f(0,0,0),       affordanceMsg.aff.bounding_rpy);
            convert3(Vector3f(0.5,0.5,0.5), affordanceMsg.aff.bounding_lwh); // TODO
          }else{
            cout << "UIProcessing::handleAffordancePubButtonPointCloud error: " << type << " not recognized\n";
            exit(-1);
          }

    // read in pcd
    /* TODO 
       - support multiple files
       - support ply files
       - cache models
    */
    pcl::PointCloud<pcl::PointXYZRGB>::Ptr modelcloud(new pcl::PointCloud<pcl::PointXYZRGB>());
    pcl::PCDReader reader;
    string file = getenv("HOME") + string("/drc/software/models/otdf/") + modelfile;
    reader.read(file.c_str(), *modelcloud);
    // Transform point cloud to match model
    /* for old car, no longer needed       
    Affine3f transformModel = Affine3f::Identity();
    transformModel.translation() = Vector3f(0.72,0,0); // correct model  TODO fix model and remove hard coding
    transformModel.linear() = ypr2rot(Vector3f(M_PI,0,0));
    transformPointCloud(*modelcloud, *modelcloud, transformModel);
    */

    //geometrical properties
    ObjectPointsPtr currObj = getCurrentObjectSelected();
    Vector3f xyz(0,0,0),ypr(0,0,0);
    //std::vector<double> inliers_distances; TODO
    //std::vector< vector<float> > inliers; TODO
    // PointIndices::Ptr inlierIndices = TODO
    vector<pcl::PointCloud<pcl::PointXYZRGB> > clouds;


    if(true) {

      if(usefitPointCloudFPC){
        Segmentation::fitPointCloudFPC(_surrogate_renderer._display_info.cloud,
                                    currObj->indices, fp, 
                                    isInitialSet, initialXYZ, initialYPR, 
                                    modelcloud,
                                    xyz, ypr, clouds);
      }else{
        Segmentation::fitPointCloud(_surrogate_renderer._display_info.cloud,
                                    currObj->indices, fp, 
                                    isInitialSet, initialXYZ, initialYPR, 
                                    modelcloud,
                                    xyz, ypr, clouds);
      }
    }else{
      Segmentation::FpfhParams fpfhParams;
      fpfhParams.minSampleDistance = 0.05;
      fpfhParams.maxCorrespondenceDistance = 0.25;
      fpfhParams.maxIterations = 500;
      fpfhParams.numberOfSamples = 3;
      Segmentation::fitPointCloudFpfh(_surrogate_renderer._display_info.cloud,
                                  currObj->indices, fp, 
                                  isInitialSet, initialXYZ, initialYPR, 
                                  modelcloud,
                                  xyz, ypr, clouds, fpfhParams);
    }

    /*
    // lcmgl display for debugging
    float colors[][3] = {{1,0,0},{0,1,0},{0,0,1},{0,1,1},{1,0,1},{1,1,0},{1,1,1}};
    for(int j=0; j<clouds.size();j++){
      bot_lcmgl_color3f(_lcmgl, colors[j][0],colors[j][1],colors[j][2]);
      bot_lcmgl_begin(_lcmgl, LCMGL_POINTS);
      for(int i=0;i<clouds[j].size();i++){
        bot_lcmgl_vertex3f(_lcmgl, clouds[j][i].x, clouds[j][i].y, clouds[j][i].z);
      }
      bot_lcmgl_end(_lcmgl);
    }
    bot_lcmgl_switch_buffer(_lcmgl);
    */

	  affordanceMsg.aff.nparams = affordanceMsg.aff.params.size();

    // set origin and bounding
    convert3(xyz,                   affordanceMsg.aff.origin_xyz);
    convert3(ypr.reverse(),         affordanceMsg.aff.origin_rpy);

    // set modelfile
    affordanceMsg.aff.modelfile = modelfile;

    affordanceMsg.npoints = 0;
    affordanceMsg.ntriangles = 0;

	  cout << "\n npoints = " << affordanceMsg.npoints << endl;

	  cout << "states.size() = " << affordanceMsg.aff.states.size() <<  " | state_names.size() = "
	       << affordanceMsg.aff.param_names.size() << endl;

	  //todo : Set these
	  //states: todo? is this used? states/state_names
	  affordanceMsg.aff.nstates = 0;
	  
    cout << "\n about to publish" << endl;
    if(selectedAff){
      affordanceMsg.aff.aff_store_control = drc::affordance_t::UPDATE;
      affordanceMsg.aff.uid = selectedAff->aff.uid;
      _lcmCpp->publish("AFFORDANCE_PLUS_TRACK", &affordanceMsg);
    }else{
      affordanceMsg.aff.aff_store_control =drc::affordance_t::NEW; 
	    _lcmCpp->publish("AFFORDANCE_FIT", &affordanceMsg);
    }
    cout << "\n ***published \n" << endl;
	  
	  return;
	}

	void UIProcessing::handleFullResetButton(BotGtkParamWidget *pw)
	{
		_gui_state = SEGMENTING;
		UserSegmentation *segInfo = _surrogate_renderer.getSegInfo();
		segInfo->objectPtsMap->clear();
		_objTracker = ObjectTracker::Ptr();
		_surrogate_renderer.initVals();
		bot_gtk_param_widget_set_bool(pw, PARAM_NAME_CLOUD_PAUSE, 0);
		bot_gtk_param_widget_set_enum(pw, PARAM_NAME_MOUSE_MODE, CAMERA_MOVE);
		bot_gtk_param_widget_set_enum(pw, PARAM_NAME_TRACK_METHOD, ICP);
		bot_gtk_param_widget_clear_enum(pw, PARAM_NAME_CURR_OBJECT);
		_surrogate_renderer.setCamera(CAMERA_FRONT);
	}


  void UIProcessing::handleAffordanceSelectChange(BotGtkParamWidget *pw)
  {
    // if selection menu is being update, don't do anything
    if(_updatingAffordanceSelectionMenu) return;

    // update _selectedAffordanceName    
    int index = bot_gtk_param_widget_get_enum(pw, PARAM_NAME_AFFORDANCE_SELECT);
    index = index-1; // 0==none, so subtract 1 for index
    if(index<0) { // "New" selected, so clear selection
      _selectedAffordanceName.clear();
      cout << "Affordance selection reset\n";
    }else{   // object selected, so make copy of affordance
      boost::lock_guard<boost::mutex> lock(_currentAffordancesMutex);
      _selectedAffordanceName = _currentAffordanceNames[index];
      string otdf_type = _currentAffordances[index].aff.otdf_type;
      cout << _selectedAffordanceName << " selected" << endl;
    
      // change geometric_primitive to match selection
      int prim = -1;
      cout << otdf_type << endl;
      if     (otdf_type == "cylinder") prim = CYLINDER;
      else if(otdf_type == "sphere")   prim = SPHERE;
      else if(otdf_type == "plane")    prim = PLANE;
      else if(otdf_type == "circle3d") prim = CIRCLE_3D; 
      else if(otdf_type == "steering_cyl") prim = STEERING_CYL; 
      else if(otdf_type == "car")      prim = CAR;
      else if(otdf_type == "standpipe::mate::firehose") prim = STANDPIPE;
      else if(otdf_type == "TODO")     prim = LINE;      // TODO
      else if(otdf_type == "TODO")     prim = TORUS;     // TODO
      else if(otdf_type == "TODO")     prim = CUBE;      // TODO
      if(prim>=0) bot_gtk_param_widget_set_enum(_surrogate_renderer._pw, PARAM_NAME_GEOMETRIC_PRIMITIVE, prim);

    }

  }

	//=========main handler for menu changes, button clicks, etc
	void UIProcessing::on_param_widget_changed_xyzrgb (BotGtkParamWidget *pw, const char *name, void *user)
	{
		if (!_surrogate_renderer._viewer)
			return;

		UserSegmentation *segInfo = _surrogate_renderer.getSegInfo();
		TrackingInfo *trackInfo = _surrogate_renderer.getTrackInfo();

		//pause
		if (stringsEqual(name, PARAM_NAME_CLOUD_PAUSE))
		{
			if (_surrogate_renderer.isPaused())
				return; //no state to update

			//not paused, so get rid of segmentation info
			_surrogate_renderer._display_info.user_seg_info.numObjects = 0;
			_surrogate_renderer._display_info.user_seg_info.objectPtsMap->clear();
			_surrogate_renderer.getModelInfo()->circles.clear();
			_surrogate_renderer.getModelInfo()->lines.clear();
			_surrogate_renderer.getModelInfo()->drawCircles = false;
			_surrogate_renderer.getModelInfo()->drawLines = false;
			bot_gtk_param_widget_clear_enum(pw, PARAM_NAME_CURR_OBJECT); //cleared objects, so clean out the enum
			//_surrogate_renderer.setHintText("Object Segmentation Cleared");
			return;
		}

		if (stringsEqual(name, PARAM_NAME_POINT_COLOR))
		{
  		bot_viewer_request_redraw(_surrogate_renderer._viewer);
    }

		//clear warnings
		if (stringsEqual(name, PARAM_NAME_CLEAR_WARNING_MSGS))
		{
			_surrogate_renderer.setWarningText("");
			//return;
		}

		//Camera Move or Selection Mode ?
		if (stringsEqual(name, PARAM_NAME_MOUSE_MODE))
		{
			handleMouseModeAdjust((BotEventHandler *) _surrogate_renderer._viewer->default_view_handler->user,
								 pw);
			//return;
		}

		// current object
		if (stringsEqual(name, PARAM_NAME_CURR_OBJECT))
		{
			if (_gui_state == TRACKING)
			{
				//old warning; since we now clear the objects list
				//_surrogate_renderer.setWarningText("Can't change current object while tracking! TODO: switch back");
				//todo:
				// reverse the effect
				//_gui_state = SEGMENTING;
				//bot_gtk_param_widget_set_enum(pw, PARAM_NAME_CURR_OBJECT, getCurrentObjectSelected());
				//_gui_state = TRACKING;
				//return;
			}

			//see if none selected
			if (stringsEqual(getCurrentObjectSelectedName(), "None")) //no object selected?
			{
				trackInfo->displayTrackingCloud 	= false;
				//return;
			}

			//return;
		}

		// new object
		if (stringsEqual(name, PARAM_NAME_NEW_OBJECT))
		{
			if (_gui_state == SEGMENTING)
			{
				segInfo->numObjects++;
				char newName[50];
				sprintf(newName, "Object %d", segInfo->numObjects);
				bot_gtk_param_widget_modify_enum(pw, PARAM_NAME_CURR_OBJECT, newName, segInfo->numObjects);

				//add object into the map
				ObjectPointsPtr newObjPts (new ObjectPoints(Color::getColor(segInfo->numObjects)));
				segInfo->objectPtsMap->insert(make_pair(newName, newObjPts));
			}
			else
			{
				_surrogate_renderer.setWarningText("Can't create new object while tracking!");
			}
			//return;
		}
		// clear object
		if (stringsEqual(name, PARAM_NAME_CLEAR_OBJECT))
		{
			if (_gui_state == SEGMENTING)
			{
				ObjectPointsPtr currObj = getCurrentObjectSelected();

				if (currObj == ObjectPointsPtr() || currObj->indices->size() == 0)
					_surrogate_renderer.setWarningText("Nothing to clear");
				else
				{
					currObj->indices->clear();
					currObj->auto_segment_indices.clear();
				}
			}
			else
			{
				_surrogate_renderer.setWarningText("Can't reset object while tracking!");
			}
			//return;
		}
		if (stringsEqual(name, PARAM_NAME_PULL_MAP))
		{
			std::vector<maps::ViewClient::ViewPtr> views = _mViewClient->getAllViews();
			cout << "Views:" << views.size() <<endl;
			if (views.size() > 0) 
			{
				maps::PointCloud::Ptr cloudFull(new maps::PointCloud());
				for (size_t v = 0; v < views.size(); ++v)
				{
          cout << "Map ID: " << views[v]->getId() << ": ";
          if(views[v]->getId() == drc::data_request_t::DEPTH_MAP_WORKSPACE  
              || views[v]->getId() > 9999   // some of the old messages have invalid view_id
              || views[v]->getId() == 10)   // this is the old number for DEPTH_MAP_WORKSPACE
          {
          cout << "Used\n";
					  maps::PointCloud::Ptr cloud = views[v]->getAsPointCloud();
                                          /*
                                            Matrix4f transformation = Matrix4f::Identity();
                                            transformation(0,3)=3;
                                            transformation(1,3)=-1;
                                            transformPointCloud(*cloud, *cloud, transformation);
                                          */
					  (*cloudFull) += *cloud;
          }else cout << "Ignored\n";
				}
				getDisplayInfo()->cloud = cloudFull;
				getDisplayInfo()->lcmCloudFrameId = "header_not_set";//msg->header.frame_id;
				cout << "Grabbed a point cloud of " << getDisplayInfo()->cloud->points.size() <<
					" points from octree" << endl;
			}

      // clear selection
		  ObjectPointsPtr currObj = getCurrentObjectSelected();
		  if (currObj->indices->size() != 0)
		  {
			  currObj->indices->clear();
			  currObj->auto_segment_indices.clear();
		  }

		}


		// tracking mode
		if (stringsEqual(name, PARAM_NAME_TRACK_METHOD))
		{
			if (_gui_state == TRACKING)
				_surrogate_renderer.setWarningText("attempted to change tracking method while already tracking");
			//return;
		}

		// live tracking button
		if (stringsEqual(name, PARAM_NAME_TRACK_OBJECTS))
		{
			handleTrackLiveButton(pw);
			//return;
		}

		// master reset button
		if (stringsEqual(name, PARAM_NAME_RESET))
		{
			handleFullResetButton(pw);
			//return;
		}

		if (stringsEqual(name, PARAM_NAME_AFFORDANCE_PUB))
		{
			handleAffordancePubButton(pw);
			//return;
		}

		if (stringsEqual(name, PARAM_NAME_AFFORDANCE_PUSH))
		{
			handleAffordancePushButton(pw);
			//return;
		}

		if (stringsEqual(name, PARAM_NAME_SAVE_CLOUD))
		{
			handleSaveCloudButton(pw);
			//return;
		}

    if (stringsEqual(name, PARAM_NAME_AFFORDANCE_SELECT)){
      handleAffordanceSelectChange(pw);
    }

		bot_viewer_request_redraw(_surrogate_renderer._viewer);
	}


	//======================================
	//======================================
	//======================================
	//===============================observers
	/**@return RECTANGLE Or CAMERA_MOVE*/
	MouseMode UIProcessing::getMouseMode() const //rectangle or camera move
	{
		int mouse_mode = bot_gtk_param_widget_get_enum(_surrogate_renderer._pw, PARAM_NAME_MOUSE_MODE);
		return (MouseMode) mouse_mode;
	}

	GeometricPrimitive UIProcessing::getGeometricPrimitive() const //rectangle or camera move
	{
		int geometric_primitive = bot_gtk_param_widget_get_enum(_surrogate_renderer._pw, PARAM_NAME_GEOMETRIC_PRIMITIVE);
		return (GeometricPrimitive) geometric_primitive;
	}

	string UIProcessing::getCurrentObjectSelectedName() const
	{
		return _surrogate_renderer.getCurrentObjectSelectedName();
	}

	/**@returns the object currently selected in the drop-down menu.  Or
	 * null if no object selected*/
	ObjectPointsPtr UIProcessing::getCurrentObjectSelected()
	{
		return _surrogate_renderer.getCurrentObjectSelected();
	}

	SurrogateDisplayInfo *UIProcessing::getDisplayInfo()
	{
		return &(_surrogate_renderer._display_info);
	}

	TrackMode UIProcessing::getTrackMode() const
	{
		int trackMode = bot_gtk_param_widget_get_enum(_surrogate_renderer._pw, PARAM_NAME_TRACK_METHOD);
		return (TrackMode) trackMode;
	}

	/**@return currently selected manipulation action type (in drop-down menu)*/
	int8_t UIProcessing::getManipulationType() const
	{
		return (int8_t) bot_gtk_param_widget_get_enum(_surrogate_renderer._pw, PARAM_NAME_MANIP_TYPE);
	}

	//==================static helpers
	bool UIProcessing::stringsEqual(string a, string b)
	{
		return (a.compare(b) == 0);
	}

	//==========================================================
	//==========================================================
	//==========================================================
	//==========================================================
	//==========================================================
	//==========================================================
	//=================================callback conversion

	void UIProcessing::cb_right_click_popup_menu(GtkWidget *menuItem, gpointer userdata)
	{
		((UIProcessing*) userdata)->right_click_popup(menuItem);
	}

	void UIProcessing::cb_on_param_widget_changed_xyzrgb (BotGtkParamWidget *pw, const char *name, void *user)
	{
		((UIProcessing*) user)->on_param_widget_changed_xyzrgb(pw, name, user);
	}

	int UIProcessing::cb_mouse_press(BotViewer *viewer, BotEventHandler *ehandler,
						const double ray_start[3], const double ray_dir[3], const GdkEventButton *event)
	{
		return ((UIProcessing*) ehandler->user)->mouse_press(viewer, ehandler, ray_start, ray_dir, event);
	}
	
	int UIProcessing::cb_mouse_release (BotViewer *viewer, BotEventHandler *ehandler,
							 const double ray_start[3], const double ray_dir[3], const GdkEventButton *event)
	{
		return ((UIProcessing*) ehandler->user)->mouse_release(viewer, ehandler, ray_start, ray_dir, event);
	}

	int UIProcessing::cb_mouse_motion(BotViewer *viewer, BotEventHandler *ehandler,
							 const double ray_start[3], const double ray_dir[3], const GdkEventMotion *event)
	{
		return ((UIProcessing*) ehandler->user)->mouse_motion(viewer, ehandler, ray_start, ray_dir, event);
	}

	int UIProcessing::cb_key_press (BotViewer *viewer, BotEventHandler *ehandler, const GdkEventKey *event)
	{
		return ((UIProcessing*) ehandler->user)->key_press(viewer, ehandler, event);
	}

	int UIProcessing::cb_key_release (BotViewer *viewer, BotEventHandler *ehandler, const GdkEventKey  *event)
	{
		return ((UIProcessing*) ehandler->user)->key_release(viewer, ehandler, event);
	}

	void UIProcessing::cb_on_kinect_frame (const lcm_recv_buf_t *rbuf, const char *channel,
					       const ptools_pointcloud2_t *msg, void *user_data)
	{
		return ((UIProcessing*) user_data)->on_kinect_frame(rbuf, channel, msg, user_data);
	}

  void UIProcessing::affordanceMsgHandler(const lcm::ReceiveBuffer* iBuf,
                const std::string& iChannel, 
                const drc::affordance_plus_collection_t* collection)
  {
    // lock cause updating _currentAffordances and _currentAffordanceNames
    boost::unique_lock<boost::mutex> lock(_currentAffordancesMutex);

    // make copy of latest affordance collection
    _currentAffordances = collection->affs_plus;

    // see if size has changed
    bool changed = false;
    if(_currentAffordances.size()!=_currentAffordanceNames.size()){
      changed = true;
      _currentAffordanceNames.resize(_currentAffordances.size());
    }

    // see if names have changed
    for(int i=0;i<_currentAffordances.size();i++) {
      drc::affordance_t& aff = _currentAffordances[i].aff;
      stringstream ss;
      ss << aff.otdf_type << "_" << aff.uid;
      string name = ss.str();
      if(name != _currentAffordanceNames[i]){
        changed = true;
        _currentAffordanceNames[i] = name;
      }
    }
    vector<string> affNames;
    if(changed) affNames = _currentAffordanceNames;
    lock.unlock();  // done with shared variable, so unlock
    
    // update menu
    if(changed){
      _updatingAffordanceSelectionMenu = true; // suppress menu callback while updating menu
		  BotGtkParamWidget *pw = _surrogate_renderer._pw;
      int selectedIndex = -1;
      bot_gtk_param_widget_clear_enum(pw, PARAM_NAME_AFFORDANCE_SELECT);
      for(int i=0; i<affNames.size(); i++){
        bot_gtk_param_widget_modify_enum(pw, PARAM_NAME_AFFORDANCE_SELECT, affNames[i].c_str(), i+1); //+1 because 0 is None
        if(_selectedAffordanceName == affNames[i]) selectedIndex = i;
      }
      bot_gtk_param_widget_set_enum(pw, PARAM_NAME_AFFORDANCE_SELECT, selectedIndex+1); //+1 because 0 is None
      if(selectedIndex == -1) _selectedAffordanceName.clear(); // clear selected name if no longer exists
      _updatingAffordanceSelectionMenu = false;
    }

  }

  // returns copy of affordance matching _selectedAffordanceName
  UIProcessing::AffPlusPtr UIProcessing::getSelectedAffordance(){
    boost::lock_guard<boost::mutex> lock(_currentAffordancesMutex);
    AffPlusPtr selectedAff;
    for(int i=0;i<_currentAffordances.size();i++) {
      drc::affordance_t& aff = _currentAffordances[i].aff;
      stringstream ss;
      ss << aff.otdf_type << "_" << aff.uid;
      string name = ss.str();
      if(name == _selectedAffordanceName){  // if name matches, make a copy
        selectedAff.reset(new drc::affordance_plus_t(_currentAffordances[i]));
      }
    }
    return selectedAff;
  }

} //namespace surrogate_gui