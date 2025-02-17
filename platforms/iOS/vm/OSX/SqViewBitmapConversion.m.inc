/* SqViewBitmapConversion.m.inc created by marcel on Fri 04-Dec-1998 */
//  CocoaSqueak
//
// John Notes: Waste not want not. 

/*
 From: Marcel Weiher <marcel.weiher@gmail.com>
 To: johnmci@smalltalkconsulting.com
 Subject: Re: [squeak-dev] Squeak and the iPhone
 Reply-To: Marcel Weiher <marcel.weiher@gmail.com>,
 The general-purpose Squeak developers list <squeak-dev@lists.squeakfoundation.org>
 Date: Wed, 18 Jun 2008 12:00:05 -0700
 Cc: The general-purpose Squeak developers list <squeak-dev@lists.squeakfoundation.org>
 
 The source code has been made available again at
 
 http://www.metaobject.com/downloads/Squeak/
 ...
 In the meantime, you have my express  
 permission to use it under an MIT license.
 
 */

#undef debug
#undef CHECKANDRETURN

@implementation ContentViewClass (SqViewBitmapConversion)

#define debug(a)

#define CHECKANDRETURN(expr) \
	{ NSBitmapImageRep* bitmap=expr; if(bitmap!=NULL){ CGImageRef ref = [bitmap CGImage]; return(ref);}\
        NSLog(@"initData:%08x\npixelsWide:%d\npixelsHigh:%d\nbitsPerSample:%d\nsamplesPerPixel:%d\nhasAlpha:%d\nisPlanar:%d\ncolorSpaceName:%@\nbytesPerRow:%d\nbitsPerPixel:%d\n",\
				(int)dBits,right-left,bottom-top,bitsPerSample,\
				samplesPerPixel,NO,NO,colorSpace,bytesPerRow,bitsPerPixel);\
		return(0);\
	}
	
-(CGImageRef)extractPixels_1_to_32:(void*)srcBits 
					 srcPixelWidth:(int)srcPixelWidth height: (int) height
							  left:(int)left right:(int)right 
							   top:(int)top bottom:(int)bottom
						  colorMap: (unsigned int *) colorMap
						tempMemory: (void **) tempMemory

{
	int                 bitsPerSample=8;
	int                 samplesPerPixel=3;
	int                 bitsPerPixel=32;
	int                 bytesPerRow=bytesPerLine(srcPixelWidth, 32);
	NSString*			colorSpace=NSDeviceRGBColorSpace;
	size_t				totalSize=bytesPerRow * ((bottom-top)+1);
	int*				dBits=(int*)malloc(totalSize);
	*tempMemory = dBits;

	
	copyImage1To32(srcBits, dBits, srcPixelWidth, height, left, top, right, bottom,colorMap);
	
	
	CHECKANDRETURN(AUTORELEASEOBJ([[NSBitmapImageRep alloc] initWithBitmapDataPlanes:(unsigned char **)&dBits
														   pixelsWide:right-left
														   pixelsHigh:bottom-top
														bitsPerSample:bitsPerSample
													  samplesPerPixel:samplesPerPixel
															 hasAlpha:NO
															 isPlanar:NO
													   colorSpaceName:colorSpace
														  bytesPerRow:bytesPerRow
														 bitsPerPixel:bitsPerPixel]));
}//extractPixels_1_to_32;


-(CGImageRef)extractPixels_2_to_32:(void*)srcBits 
					 srcPixelWidth:(int)srcPixelWidth height: (int) height
							  left:(int)left right:(int)right 
							   top:(int)top bottom:(int)bottom
						  colorMap: (unsigned int *) colorMap
						tempMemory: (void **) tempMemory
{
	int                 bitsPerSample=8;
	int                 samplesPerPixel=3;
	int                 bitsPerPixel=32;
	int                 bytesPerRow=bytesPerLine(srcPixelWidth, 32);
	NSString*			colorSpace=NSDeviceRGBColorSpace;
	size_t				totalSize=bytesPerRow * ((bottom-top)+1);
	int*				dBits=(int*)malloc(totalSize);
	*tempMemory = dBits;

	copyImage2To32(srcBits, dBits, srcPixelWidth, height, left, top, right, bottom,colorMap);
	
	
	CHECKANDRETURN(AUTORELEASEOBJ([[NSBitmapImageRep alloc] initWithBitmapDataPlanes:(unsigned char **)&dBits
														   pixelsWide:right-left
														   pixelsHigh:bottom-top
														bitsPerSample:bitsPerSample
													  samplesPerPixel:samplesPerPixel
															 hasAlpha:NO
															 isPlanar:NO
													   colorSpaceName:colorSpace
														  bytesPerRow:bytesPerRow
														 bitsPerPixel:bitsPerPixel]));
}//extractPixels_2_to_32;

-(CGImageRef)extractPixels_4_to_32:(void*)srcBits 
					 srcPixelWidth:(int)srcPixelWidth height: (int) height
							  left:(int)left right:(int)right 
							   top:(int)top bottom:(int)bottom
						  colorMap: (unsigned int *) colorMap
						tempMemory: (void **) tempMemory
{
	int                 bitsPerSample=8;
	int                 samplesPerPixel=3;
	int                 bitsPerPixel=32;
	int                 bytesPerRow=bytesPerLine(srcPixelWidth, 32);
	NSString*			colorSpace=NSDeviceRGBColorSpace;
	size_t				totalSize=bytesPerRow * ((bottom-top)+1);
	int*				dBits=(int*)malloc(totalSize);
	*tempMemory = dBits;

	copyImage4To32(srcBits, dBits, srcPixelWidth, height, left, top, right, bottom,colorMap);
	
	
	CHECKANDRETURN(AUTORELEASEOBJ([[NSBitmapImageRep alloc] initWithBitmapDataPlanes:(unsigned char **)&dBits
														   pixelsWide:right-left
														   pixelsHigh:bottom-top
														bitsPerSample:bitsPerSample
													  samplesPerPixel:samplesPerPixel
															 hasAlpha:NO
															 isPlanar:NO
													   colorSpaceName:colorSpace
														  bytesPerRow:bytesPerRow
														 bitsPerPixel:bitsPerPixel]));
}//extractPixels_4_to_32;


-(CGImageRef)extractPixels_8_to_32:(void*)srcBits 
					 srcPixelWidth:(int)srcPixelWidth height: (int) height
			left:(int)left right:(int)right 
			top:(int)top bottom:(int)bottom
						  colorMap: (unsigned int *) colorMap
						tempMemory: (void **) tempMemory
	{
		int                 bitsPerSample=8;
		int                 samplesPerPixel=3;
		int                 bitsPerPixel=32;
		int                 bytesPerRow=bytesPerLine(srcPixelWidth, 32);
		NSString*			colorSpace=NSDeviceRGBColorSpace;
		size_t				totalSize=bytesPerRow * ((bottom-top)+1);
		int*				dBits=(int*)malloc(totalSize);
		*tempMemory = dBits;

		copyImage8To32(srcBits, dBits, srcPixelWidth, height, left, top, right, bottom,colorMap);
		
		
		CHECKANDRETURN(AUTORELEASEOBJ([[NSBitmapImageRep alloc] initWithBitmapDataPlanes:(unsigned char **)&dBits
				pixelsWide:right-left
				pixelsHigh:bottom-top
				bitsPerSample:bitsPerSample
				samplesPerPixel:samplesPerPixel
				hasAlpha:NO
				isPlanar:NO
				colorSpaceName:colorSpace
				bytesPerRow:bytesPerRow
				bitsPerPixel:bitsPerPixel]));
	}//extractPixels_8_to_32;
	

-(CGImageRef)extractPixels_16_to_32:(void*)srcBits 
					  srcPixelWidth:(int)srcPixelWidth height: (int) height
							   left:(int)left right:(int)right 
								top:(int)top bottom:(int)bottom
						 tempMemory: (void **) tempMemory
{
	int                 bitsPerSample=8;
	int                 samplesPerPixel=3;
	int                 bitsPerPixel=32;
	int                 bytesPerRow=bytesPerLine(srcPixelWidth, 32);
	NSString*			colorSpace=NSDeviceRGBColorSpace;
	size_t				totalSize=bytesPerRow * ((bottom-top)+1);
	int*				dBits=(int*)malloc(totalSize);
	*tempMemory = dBits;
	
	copyImage16To32(srcBits, dBits, srcPixelWidth, height, left, top, right, bottom);
	
	CHECKANDRETURN(AUTORELEASEOBJ([[NSBitmapImageRep alloc] initWithBitmapDataPlanes:(unsigned char **)&dBits
														   pixelsWide:right-left
														   pixelsHigh:bottom-top
														bitsPerSample:bitsPerSample
													  samplesPerPixel:samplesPerPixel
															 hasAlpha:NO
															 isPlanar:NO
													   colorSpaceName:colorSpace
														  bytesPerRow:bytesPerRow
														 bitsPerPixel:bitsPerPixel]));
}//extractPixels_16_to_32;

-(CGImageRef)computeBitmapFromBitsIndex:(void*)srcBits
                                                        width:(int)width height:(int)height depth:(int)depth
                                                         left:(int)left right:(int)right
                                                          top:(int)top bottom:(int)bottom
															tempMemory: (void**) tempMemory
{
    /*
     It seems that NSImage will replace its NSBitmapImageRep with a
     NXCachedImageRep of the whole bitmap upon receiving a
     composite:fromRect:toPoint: message.
     Therefore:
              1- it is useless to keep our own "cached" image;
              2- we may as well copy to the right format & depth the affected
     rectangle in a temporary bitmap, and draw it directly.
     */
    debug(int dummy=fprintf(stderr,"%s\n",sel_getName(_cmd));\
          int fummy=fflush(stderr);)
    switch(depth){
        case 1:
  			return([self extractPixels_1_to_32:srcBits srcPixelWidth:width height: height
                                          left:left right:right top:top bottom:bottom
									  colorMap: colorMap32
									tempMemory: tempMemory]);
            break;
        case 2:
   			return([self extractPixels_2_to_32:srcBits srcPixelWidth:width height: height
                                          left:left right:right top:top bottom:bottom
									  colorMap: colorMap32
									tempMemory: tempMemory]);
			break;
		case 4:
  			return([self extractPixels_4_to_32:srcBits srcPixelWidth:width height: height
                                          left:left right:right top:top bottom:bottom
									  colorMap: colorMap32
									tempMemory: tempMemory]);
			break;
        case 8:
			return([self extractPixels_8_to_32:srcBits srcPixelWidth:width height: height
                                          left:left right:right top:top bottom:bottom
									  colorMap: colorMap32
									tempMemory: tempMemory]);
			break;
        case 16:
            return([self extractPixels_16_to_32:srcBits srcPixelWidth:width height: height
										   left:left right:right top:top bottom:bottom
									 tempMemory: tempMemory]);
			break;
        default:
            switch(NSRunAlertPanel(@"Error",
                                   @"Cocoa user interface for Squeak doesn't support image depth of %d.\n",
                                   @"Continue",@"Quit",NULL,depth)){
                case NSAlertDefaultReturn:
                    return(0);
                case NSAlertAlternateReturn:
                    ioExit();
                    return(0);
                case NSAlertErrorReturn:
                default:
                    NSLog(@"Cocoa user interface for Squeak doesn't support image depth of %d.\n",depth);
                    ioExit();
                    return(0);
                    }
            }
}


@end
