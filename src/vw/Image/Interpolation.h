// __BEGIN_LICENSE__
// 
// Copyright (C) 2006 United States Government as represented by the
// Administrator of the National Aeronautics and Space Administration
// (NASA).  All Rights Reserved.
// 
// Copyright 2006 Carnegie Mellon University. All rights reserved.
// 
// This software is distributed under the NASA Open Source Agreement
// (NOSA), version 1.3.  The NOSA has been approved by the Open Source
// Initiative.  See the file COPYING at the top of the distribution
// directory tree for the complete NOSA document.
// 
// THE SUBJECT SOFTWARE IS PROVIDED "AS IS" WITHOUT ANY WARRANTY OF ANY
// KIND, EITHER EXPRESSED, IMPLIED, OR STATUTORY, INCLUDING, BUT NOT
// LIMITED TO, ANY WARRANTY THAT THE SUBJECT SOFTWARE WILL CONFORM TO
// SPECIFICATIONS, ANY IMPLIED WARRANTIES OF MERCHANTABILITY, FITNESS FOR
// A PARTICULAR PURPOSE, OR FREEDOM FROM INFRINGEMENT, ANY WARRANTY THAT
// THE SUBJECT SOFTWARE WILL BE ERROR FREE, OR ANY WARRANTY THAT
// DOCUMENTATION, IF PROVIDED, WILL CONFORM TO THE SUBJECT SOFTWARE.
// 
// __END_LICENSE__

/// \file Interpolation.h
/// 
/// Image views that can be accessed with real (floating point) pixel
/// indices.  These image views will perform one of:
/// 
/// - bilinear interpolation       ( BilinearInterpolation()      )
/// - bicubic interpolation        ( BicubicInterpolation()       )
/// - nearest pixel interpolation  ( NearestPixelInterpolaiton()  )
///   (unless the underlying image view can also be accessed using real pixel values)
///
#ifndef __VW_IMAGE_INTERPOLATION_H__
#define __VW_IMAGE_INTERPOLATION_H__

#include <boost/type_traits.hpp>
#include <boost/mpl/logical.hpp>
#include <boost/utility/enable_if.hpp>

#include <vw/Core/CompoundTypes.h>
#include <vw/Math/Functions.h>
#include <vw/Image/ImageView.h>
#include <vw/Image/PixelAccessors.h>
#include <vw/Image/EdgeExtension.h>
#include <vw/Image/Manipulation.h>

namespace vw {

  /// \cond INTERNAL
  // Functors for common interpolation modes.  You may define your own
  // functor similar to those that appear below.  The functor must
  // define the call operator, which takes a view and a set of
  // (floating point) coordinates, and return the interpolated value
  // at that sub-pixel location.

  /// A base class for the interpolation types that provides the
  /// common return type deduction logic in case users want to use
  /// these types in a more general manner.
  ///
  /// pixel_buffer is the number of additional pixels to prerasterize
  /// along the edge of the child image (in case we do need to
  /// rasterize the child when pre-rasterize is called). The subclass
  /// of InterpolationBase _must_ override this value and set it to
  /// the number of pixels that the interpolation algorithm will need
  /// to search outside the boundaries of the image on each side.
  struct InterpolationBase {
    static const int32 pixel_buffer = 0; 
    template <class ArgsT> struct result {};
    template <class FuncT, class ViewT, class IT, class JT, class PT>
    struct result<FuncT(ViewT,IT,JT,PT)> {
      typedef typename boost::remove_reference<ViewT>::type::pixel_type type;
    };
  };

  // Bilinear interpolation operator
  struct BilinearInterpolation : InterpolationBase {
    static const int32 pixel_buffer = 1; 
    template <class ViewT>
    inline typename ViewT::pixel_type operator()(const ViewT &view, double i, double j, int32 p ) const { 
      typedef typename ViewT::pixel_type pixel_type;
      typedef typename CompoundChannelType<pixel_type>::type channel_type;
      typedef typename FloatType<channel_type>::type real_type;
      typedef typename CompoundChannelCast<pixel_type,real_type>::type result_type;

      int32 x = math::impl::_floor(i), y = math::impl::_floor(j);
      real_type normx = i-x, normy = j-y;

      result_type result = (view(x,y,p)   * (1-normy) + view(x,y+1,p)   * normy) * (1-normx) +
                           (view(x+1,y,p) * (1-normy) + view(x+1,y+1,p) * normy) * normx;

      return channel_cast_round_and_clamp_if_int<channel_type>(result);
    }
  };

  // Bicubic interpolation operator
  struct BicubicInterpolation : InterpolationBase {
    static const int32 pixel_buffer = 2; 
    template <class ViewT>
    inline typename ViewT::pixel_type operator()( const ViewT &view, double i, double j, int32 p ) const { 
      typedef typename ViewT::pixel_type pixel_type;
      typedef typename CompoundChannelType<pixel_type>::type channel_type;
      typedef typename CompoundChannelCast<pixel_type,double>::type result_type;
      
      int32 x = math::impl::_floor(i), y = math::impl::_floor(j);
      double normx = i-x, normy = j-y;
      
      double s0 = ((2-normx)*normx-1)*normx;      double t0 = ((2-normy)*normy-1)*normy;
      double s1 = (3*normx-5)*normx*normx+2;      double t1 = (3*normy-5)*normy*normy+2;
      double s2 = ((4-3*normx)*normx+1)*normx;    double t2 = ((4-3*normy)*normy+1)*normy;
      double s3 = (normx-1)*normx*normx;          double t3 = (normy-1)*normy*normy;
      
      typename ViewT::pixel_accessor acc = view.origin().advance(x-1,y-1,p);
      result_type row =         s0*(*acc);
      acc.next_col();    row += s1*(*acc);
      acc.next_col();    row += s2*(*acc);
      acc.next_col();    row += s3*(*acc);
      result_type result =      t0*row;
      acc.advance(-3,1); row =  s0*(*acc);
      acc.next_col();    row += s1*(*acc);
      acc.next_col();    row += s2*(*acc);
      acc.next_col();    row += s3*(*acc);
      result +=                 t1*row;
      acc.advance(-3,1); row =  s0*(*acc);
      acc.next_col();    row += s1*(*acc);
      acc.next_col();    row += s2*(*acc);
      acc.next_col();    row += s3*(*acc);
      result +=                 t2*row;
      acc.advance(-3,1); row =  s0*(*acc);
      acc.next_col();    row += s1*(*acc);
      acc.next_col();    row += s2*(*acc);
      acc.next_col();    row += s3*(*acc);
      result +=                 t3*row;
      result *= 0.25;

      return channel_cast_round_and_clamp_if_int<channel_type>( result );
    }
  };

  // NearestPixel interpolation operator.  
  struct NearestPixelInterpolation : InterpolationBase {
    static const int32 pixel_buffer = 1; 
    template <class ViewT>
    inline typename ViewT::pixel_type operator()( const ViewT &view, double i, double j, int32 p ) const {
      int32 x = math::impl::_round(i), y = math::impl::_round(j);
      return view(x,y,p);
    }
  };


  /// Interpolation View Class
  ///
  /// An image view that excepts real numbers as pixel coordinates and
  /// interpolates the value of the image at these coordinates using
  /// some interpolation method.  For pixels that fall outside the range
  /// of the wrapped image view, an newly constructed (empty) pixeltype
  /// is returned.
  template <class ImageT, class InterpT>
  class InterpolationView : public ImageViewBase<InterpolationView<ImageT, InterpT> >
  {
  private:
    ImageT m_image;
    InterpT m_interp_func;
  public:

    typedef typename ImageT::pixel_type pixel_type;
    typedef pixel_type result_type;
    typedef ProceduralPixelAccessor<InterpolationView<ImageT, InterpT> > pixel_accessor;
    
    InterpolationView( ImageT const& image, 
                       InterpT const& interp_func = InterpT()) :
      m_image(image), m_interp_func(interp_func) {}

    inline int32 cols() const { return m_image.cols(); }
    inline int32 rows() const { return m_image.rows(); }
    inline int32 planes() const { return m_image.planes(); }

    inline pixel_accessor origin() const { return pixel_accessor(*this, 0, 0); }

    inline result_type operator() (double i, double j, int32 p = 0) const { return m_interp_func(m_image,i,j,p); }

    /// \cond INTERNAL
    // We can make an optimization here.  If the pixels in the child
    // view cannot be repeatedly accessed without incurring any
    // additional overhead  then we should rasterize the child 
    // before we proceed to rasterize ourself.
    typedef typename boost::mpl::if_< IsMultiplyAccessible<ImageT>, 
 				      InterpolationView<typename ImageT::prerasterize_type, InterpT>,
 				      InterpolationView<CropView<ImageView<pixel_type> >, InterpT> >::type prerasterize_type;

    template <class PreRastImageT>
    prerasterize_type prerasterize_helper( BBox2i bbox, PreRastImageT const& image, true_type ) const { 
      return prerasterize_type( image.prerasterize(bbox), m_interp_func ); 
    }
                            
    template <class PreRastImageT>
    prerasterize_type prerasterize_helper( BBox2i bbox, PreRastImageT const& image, false_type ) const {
      ImageView<pixel_type> buf( bbox.width(), bbox.height(), m_image.planes() );
      image.rasterize( buf, bbox );
      return prerasterize_type( CropView<ImageView<pixel_type> >( buf, BBox2i(-bbox.min().x(),-bbox.min().y(),
                                                                              image.cols(), image.rows())), m_interp_func);
    }

    inline prerasterize_type prerasterize( BBox2i bbox ) const {
      int32 padded_width = bbox.width() + 2 * m_interp_func.pixel_buffer;
      int32 padded_height = bbox.height() + 2 * m_interp_func.pixel_buffer;
      BBox2i adjusted_bbox(bbox.min().x() - m_interp_func.pixel_buffer, 
                           bbox.min().y() - m_interp_func.pixel_buffer,
                           padded_width, padded_height);
      return prerasterize_helper(adjusted_bbox, m_image, typename IsMultiplyAccessible<ImageT>::type() );
    }
  
    template <class DestT> inline void rasterize( DestT const& dest, BBox2i bbox ) const { vw::rasterize( prerasterize(bbox), dest, bbox ); }
    /// \endcond
  };
  
  /// \cond INTERNAL
  // Type traits 
  template <class ImageT, class InterpT>
  struct IsFloatingPointIndexable<InterpolationView<ImageT, InterpT> > : public true_type {};
  /// \endcond
	

  // -------------------------------------------------------------------------------
  // Functional API
  // -------------------------------------------------------------------------------

  /// Use this free function to pass in an arbitrary interpolation
  /// functor.  You can use of the predefined functors at the top of
  /// this file or use one of your own devising.  
  /// 
  /// This version of interpolate takes an extra argument, the edge
  /// extension functor, and it automatically edge extends the image
  /// before interpolating.  See EdgeExtension.h for a list of built-in
  /// functors.
  template <class ImageT, class InterpT, class EdgeExtensionT>
  InterpolationView<EdgeExtensionView<ImageT,EdgeExtensionT>, InterpT> interpolate( ImageViewBase<ImageT> const& v, 
                                                                                    InterpT const& interp_func,
                                                                                    EdgeExtensionT const& edge_extend_func) {
    return InterpolationView<EdgeExtensionView<ImageT, EdgeExtensionT>, InterpT>( edge_extend(v, edge_extend_func) , interp_func );
  }

  /// Use this free function to pass in an arbitrary interpolation
  /// functor.  You can use of the predefined functors at the top of
  /// this file or even one of your own devising.  
  /// 
  /// This version of the interpolation function uses Constant edge
  /// extension by default.
  template <class ImageT, class InterpT> InterpolationView<EdgeExtensionView<ImageT, ConstantEdgeExtension>, InterpT> 
  interpolate( ImageViewBase<ImageT> const& v, InterpT const& interp_func) {
    return InterpolationView<EdgeExtensionView<ImageT, ConstantEdgeExtension>, InterpT>( edge_extend(v, ConstantEdgeExtension()), interp_func );
  }

  /// Use this free function to pass in an arbitrary interpolation
  /// functor.  You can use of the predefined functors at the top of
  /// this file or even one of your own devising.  
  /// 
  /// This version of the interpolation function uses Constant edge
  /// extension by default.
  template <class ImageT> InterpolationView<EdgeExtensionView<ImageT, ConstantEdgeExtension>, BilinearInterpolation> 
  interpolate( ImageViewBase<ImageT> const& v ) {
    return InterpolationView<EdgeExtensionView<ImageT, ConstantEdgeExtension>, BilinearInterpolation>( edge_extend(v, ConstantEdgeExtension()), BilinearInterpolation() );
  }

} // namespace vw

#endif // __VW_IMAGE_INTERPOLATION_H__
